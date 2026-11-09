"""End-to-end checks using actual CSV trades, snapshots and independently rebuilt ledgers."""
import csv
import io
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from collections import defaultdict

exe = sys.argv[1]

def execute(*args):
    return subprocess.run([exe, *map(str, args)], check=True, text=True, capture_output=True).stdout

def key(row):
    return row['strategy'], row['scenario'], row['seed']

with tempfile.TemporaryDirectory() as directory:
    output = execute('--runs', 2, '--ticks', 80, '--trace-dir', directory)
    assert output == execute('--runs', 2, '--ticks', 80), 'runs must reproduce byte for byte'
    rows = list(csv.DictReader(io.StringIO(output)))
    assert len(rows) == 7 * 4 * 2
    trades = defaultdict(list)
    with (Path(directory) / 'trades.csv').open() as stream:
        for tr in csv.DictReader(stream):
            assert int(tr['price_cents']) > 0 and int(tr['quantity']) > 0
            assert tr['buyer'] != tr['seller'], 'no self-trades'
            trades[key(tr)].append(tr)
    snapshots = defaultdict(list)
    shared_tape = {}
    with (Path(directory) / 'snapshots.csv').open() as stream:
        for snapshot in csv.DictReader(stream):
            snapshots[key(snapshot)].append(snapshot)
            tape_key = snapshot['scenario'], snapshot['seed'], snapshot['tick']
            marks = snapshot['mark_a_cents'], snapshot['mark_b_cents']
            assert shared_tape.setdefault(tape_key, marks) == marks, 'identical exogenous paths'
            assert int(snapshot['bid_a_cents']) < int(snapshot['ask_a_cents'])
    for row in rows:
        for field, value in row.items():
            if field not in ('strategy', 'scenario') and value:
                assert math.isfinite(float(value)), (field, value)
        cash, positions, shares, events, gross = 10_000_000, {'A': 0, 'B': 0}, 0, 0, 0
        for tr in trades[key(row)]:
            if tr['buyer'] != '1' and tr['seller'] != '1':
                continue
            q = int(tr['quantity'])
            signed = q if tr['buyer'] == '1' else -q
            cash -= int(tr['price_cents']) * signed + q
            positions[tr['symbol']] += signed
            shares += q
            events += 1
            gross = max(gross, abs(positions['A']) + abs(positions['B']))
            if row['strategy'] in ('fixed_spread', 'inventory_aware', 'volatility_adaptive'):
                assert abs(positions['A']) <= 40
            elif row['strategy'] == 'twap':
                assert 0 <= positions['A'] <= 100
            else:
                assert abs(positions['A']) <= 20 and abs(positions['B']) <= 20
        path = snapshots[key(row)]
        assert len(path) == 80
        final = path[-1]
        pnl = (cash + positions['A'] * int(final['mark_a_cents']) + positions['B'] * int(final['mark_b_cents']) - 10_000_000) / 100
        assert math.isclose(pnl, float(row['pnl_dollars']), abs_tol=1e-6)
        assert cash == int(final['cash_cents'])
        assert positions['A'] == int(row['final_position_a']) == int(final['position_a'])
        assert positions['B'] == int(row['final_position_b']) == int(final['position_b'])
        assert shares == int(row['filled_shares']) and events == int(row['fill_events'])
        assert math.isclose(shares / 100, float(row['fees_dollars']), abs_tol=1e-6)
        assert gross == int(row['max_gross_inventory'])
        peak = drawdown = 0
        tick_trades = defaultdict(list)
        for tr in trades[key(row)]:
            if tr['buyer'] == '1' or tr['seller'] == '1':
                tick_trades[int(tr['tick'])].append(tr)
        running_cash, running_positions = 10_000_000, {'A': 0, 'B': 0}
        for snapshot in path:
            for tr in tick_trades[int(snapshot['tick'])]:
                q = int(tr['quantity'])
                signed = q if tr['buyer'] == '1' else -q
                running_cash -= int(tr['price_cents']) * signed + q
                running_positions[tr['symbol']] += signed
            assert running_cash == int(snapshot['cash_cents'])
            assert running_positions['A'] == int(snapshot['position_a'])
            assert running_positions['B'] == int(snapshot['position_b'])
            pnl_tick = (running_cash + running_positions['A'] * int(snapshot['mark_a_cents']) + running_positions['B'] * int(snapshot['mark_b_cents']) - 10_000_000) / 100
            assert math.isclose(pnl_tick, float(snapshot['pnl_dollars']), abs_tol=1e-6)
            peak = max(peak, pnl_tick)
            drawdown = max(drawdown, peak - pnl_tick)
        assert math.isclose(drawdown, float(row['max_drawdown_dollars']), abs_tol=1e-6)
        if row['strategy'] == 'twap':
            assert positions['A'] == 100 and float(row['completion_fraction']) == 1
            buys = [tr for tr in trades[key(row)] if tr['buyer'] == '1' and tr['symbol'] == 'A']
            cost = sum(int(tr['price_cents']) * int(tr['quantity']) for tr in buys)
            assert math.isclose(cost / 100 - int(path[0]['mark_a_cents']), float(row['arrival_slippage_cents']), abs_tol=1e-6)
        else:
            assert row['completion_fraction'] == row['arrival_slippage_cents'] == ''
    solo = execute('--strategy', 'pairs', '--scenario', 'shock', '--runs', 1, '--ticks', 80)
    solo_row = next(csv.DictReader(io.StringIO(solo)))
    assert solo_row == next(row for row in rows if key(row) == ('pairs', 'shock', '42')), 'selection must not change a trial'
    execute('--strategy', 'twap', '--scenario', 'trending', '--runs', 1, '--ticks', 1, '--seed', 0)
    for args in [('--ticks', '0'), ('--runs', '-1'), ('--strategy', 'bad'), ('--scenario', 'bad'), ('--ticks', '20oops'), ('--unknown', '1'), ('--seed', '-1'), ('--seed', '4294967296'), ('--runs',)]:
        result = subprocess.run([exe, *args], text=True, capture_output=True)
        assert result.returncode != 0 and 'Error:' in result.stderr, args
print('PASS 56 seeded trials: reproducibility, shared paths, risk limits, CSV ledgers, drawdown, TWAP and CLI validation')
