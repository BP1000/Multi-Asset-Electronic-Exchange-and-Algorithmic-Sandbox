#!/usr/bin/env python3
"""Summarize independent trials; uses Python's standard library only."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import mean, stdev
from math import sqrt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('csv', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
with args.csv.open() as stream:
    rows = list(csv.DictReader(stream))
if not rows:
    parser.error('CSV has no trials')
groups = defaultdict(list)
for row in rows:
    groups[row['scenario'], row['strategy']].append(row)
lines = ['# Simulation results', '',
         f'Source: `{args.csv.name}`. {len(rows)} trials; horizons: ' + ', '.join(sorted({r['ticks'] for r in rows})) + ' ticks.', '',
         'Profit includes one cent per filled share in fees and marks open inventory at the synthetic external fair price. '
         'It excludes liquidation costs. Intervals are approximate 95% normal Monte Carlo intervals for the mean across seeds; '
         'they do not measure real-market uncertainty. Max drawdown is sampled at tick ends.', '',
         '| Scenario | Strategy | Runs | Mean P&L ($) | Approx. 95% interval ($) | Mean max drawdown ($) | Mean filled shares | Mean peak gross inventory |',
         '|---|---|---:|---:|---:|---:|---:|---:|']
for (scenario, strategy), trials in sorted(groups.items()):
    if strategy == 'twap':
        continue
    values = [float(r['pnl_dollars']) for r in trials]
    avg = mean(values)
    interval = f'{avg - 1.96 * stdev(values) / sqrt(len(values)):.2f} to {avg + 1.96 * stdev(values) / sqrt(len(values)):.2f}' if len(values) > 1 else 'N/A'
    drawdown = mean(float(r['max_drawdown_dollars']) for r in trials)
    shares = mean(float(r['filled_shares']) for r in trials)
    inventory = mean(float(r['max_gross_inventory']) for r in trials)
    lines.append(f'| {scenario} | {strategy} | {len(values)} | {avg:.2f} | {interval} | {drawdown:.2f} | {shares:.1f} | {inventory:.1f} |')
lines += ['', '## TWAP execution', '',
          'TWAP buys 100 shares. Positive arrival slippage means the fill VWAP was above the arrival fair price (worse for the buyer); '
          'this measure excludes fees. A market drift can dominate this number, so it does not isolate market impact. '
          'TWAP P&L is not ranked against signal strategies.', '',
          '| Scenario | Runs | Mean completion | Mean arrival slippage (cents/share) | Mean fees ($) |',
          '|---|---:|---:|---:|---:|']
for (scenario, strategy), trials in sorted(groups.items()):
    if strategy != 'twap':
        continue
    completion = mean(float(r['completion_fraction']) for r in trials)
    slippages = [float(r['arrival_slippage_cents']) for r in trials if r['arrival_slippage_cents']]
    slippage = f'{mean(slippages):.2f}' if slippages else 'N/A'
    fees = mean(float(r['fees_dollars']) for r in trials)
    lines.append(f'| {scenario} | {len(trials)} | {completion:.1%} | {slippage} | {fees:.2f} |')
lines += ['', '## Interpretation', '',
          'These are controlled synthetic experiments, not calibrated backtests. Background liquidity is replenished every tick, '
          'noise orders are bounded and balanced in direction, and prices follow the configured external process. '
          'A passive strategy can benefit mechanically from this setup. Low losses accompanied by few fills may simply mean low participation. '
          'Compare results by scenario, activity, fees and exposure rather than treating a profit ranking as evidence of a durable trading edge.', '']
report = '\n'.join(lines)
if args.output:
    args.output.write_text(report)
else:
    print(report)
