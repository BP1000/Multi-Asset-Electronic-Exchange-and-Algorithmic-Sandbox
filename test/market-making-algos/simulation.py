"""
One-minute simulation built around YOUR market maker (example.py).

Every agent starts with $100,000 of cash and STARTING_SHARES shares, and can
never spend more cash or sell more shares than it owns:
  - A buy order is only sent if the cash balance, minus cash already committed
    to resting buy orders, covers its full cost.
  - A sell order is only sent if the share balance, minus shares already
    committed to resting sell orders, covers its full quantity.
Otherwise the order is skipped (no shorting, no borrowing).

Agents (each on its own socket):
  - MarketMaker : your class from example.py
  - Noise       : random buys/sells that sweep the book
  - Informed    : hidden true value that random-walks, trades toward it

Every second it prints the last trade price, trade count, volume and the
market maker's cash balance. A summary is printed at the end.

Usage:  python3 simulation.py [seconds]        (default 60)

Needs the server to send fill messages:
  {"type":"fill","side":"BUY"|"SELL","price":<int cents>,"qty":<int>}
Start the exchange with createNewBook("AAPL", 12000) and restart it between runs.
"""
import json
import random
import socket
import statistics
import sys
import threading
import time
from collections import deque

try:
    import example as mm_mod            # your market maker file
except ImportError:
    import example_1 as mm_mod

DURATION_SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 60
STARTING_DOLLARS = 100_000              # cash every agent starts with
STARTING_SHARES = 50000                    # shares every agent starts with

# ---- market maker settings sized for a $100,000 budget ----
mm_mod.QUOTE_SIZE = 100                 # shares per quote (about $12,000 each)
mm_mod.MAX_RESTING_QUOTES = 16          # about 8 bids + 8 asks resting
mm_mod.MAX_POSITION = 10_000_000        # cash/shares are the real limit
mm_mod.SKEW_PER_SHARE = 0.0001          # dollars of quote shift per share held
mm_mod.REFRESH_SECS = 0.1

HOST, PORT, TICKER = mm_mod.HOST, mm_mod.PORT, mm_mod.TICKER
BUY, SELL, LIMIT = mm_mod.BUY, mm_mod.SELL, mm_mod.LIMIT_ORDER
START_CENTS = mm_mod.to_ticks(mm_mod.START_PRICE)


class Wallet:
    """Cash and inventory in cents / shares.

    Buys must be fully covered by free cash, and sells must be fully covered
    by free shares, so neither cash nor inventory can ever go below zero."""

    def __init__(self, dollars, shares=0):
        self.start = dollars * 100
        self.cash = self.start           # actual cash balance
        self.reserved = 0                # cash committed to resting buys
        self.buys = deque()              # [limit price, shares left], oldest first
        self.pos = shares                # shares owned
        self.reserved_shares = 0         # shares committed to resting sells
        self.low = self.start            # lowest balance ever seen
        self.max_abs_pos = abs(shares)
        self.fills = 0
        self.traded = 0
        self.lock = threading.Lock()

    def allow(self, side, px, qty):
        """Reserve cash (buy) or shares (sell). False if not affordable."""
        if qty <= 0 or px <= 0:
            return False
        with self.lock:
            if side == BUY:
                cost = px * qty
                if self.cash - self.reserved < cost:
                    return False
                self.reserved += cost
                self.buys.append([px, qty])
                return True
            # SELL: only shares we own and haven't already offered
            if self.pos - self.reserved_shares < qty:
                return False
            self.reserved_shares += qty
            return True

    def fill(self, side, px, qty):
        with self.lock:
            if side == BUY:
                self.cash -= px * qty
                self.pos += qty
                left = qty                       # release the cash that was held
                while left and self.buys:
                    entry = self.buys[0]
                    take = min(left, entry[1])
                    self.reserved -= entry[0] * take
                    entry[1] -= take
                    left -= take
                    if entry[1] == 0:
                        self.buys.popleft()
                self.reserved = max(0, self.reserved)
            else:
                self.cash += px * qty
                self.pos -= qty
                # release the shares that were held for this sell
                self.reserved_shares = max(0, self.reserved_shares - qty)
            self.low = min(self.low, self.cash)
            self.max_abs_pos = max(self.max_abs_pos, abs(self.pos))
            self.fills += 1
            self.traded += qty

    def snapshot(self):
        with self.lock:
            return self.cash, self.pos

    def pnl(self, mark):
        """Mark-to-market profit in cents: cash change plus inventory value."""
        with self.lock:
            return self.cash - self.start + self.pos * mark


class Tape:
    """Trades seen by the whole market. Each trade has exactly one BUY-side
    fill, so only BUY fills are recorded (no double counting)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.last, self.volume, self.trades, self.prices = START_CENTS, 0, 0, []

    def record(self, px, qty):
        with self.lock:
            self.last = px
            self.volume += qty
            self.trades += 1
            self.prices.append(px)

    def snapshot(self):
        with self.lock:
            return self.last, self.volume, self.trades


class SimMarketMaker(mm_mod.MarketMaker):
    """Your MarketMaker, with a cash/share budget and fill handling that works
    with the server's side/price/qty fill message."""

    def __init__(self, tape):
        self.tape = tape
        self.wallet = Wallet(STARTING_DOLLARS, STARTING_SHARES)
        super().__init__(name="MarketMaker", capital=STARTING_DOLLARS)

    def place(self, side, qty, price):   # same as yours, plus the ownership check
        px = mm_mod.to_ticks(price)
        if not self.wallet.allow(side, px, int(qty)):
            return                       # cannot afford / don't own it, skip
        with self.state_lock:
            self.pending.append(side)
        self._send({"ticker": TICKER, "side": side, "type": LIMIT,
                    "volume": int(qty), "price": px, "stop": 0})

    def _listen(self):
        try:
            for line in self.sock.makefile("r"):
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                kind = msg.get("type")
                if kind == "ack":
                    with self.state_lock:
                        if self.pending:
                            self.pending.popleft()
                        if msg.get("id"):
                            self.live_orders.append(msg["id"])
                elif kind == "fill":
                    side = BUY if msg["side"] in ("BUY", BUY) else SELL
                    px, qty = int(msg["price"]), int(msg["qty"])
                    self.wallet.fill(side, px, qty)
                    self.tape.record(px, qty)
                    with self.state_lock:
                        self.position = self.wallet.snapshot()[1] - STARTING_SHARES  # drives skew
                        self.last = px / mm_mod.PRICE_SCALE         # quotes follow trades
                        if self.live_orders:
                            self.live_orders.pop(0)
        except (OSError, ValueError, KeyError):
            pass
        finally:
            self.connected = False


class Flow:
    """Simple client for the order-flow agents, with the same budget rules."""

    def __init__(self, name, tape):
        self.tape = tape
        self.wallet = Wallet(STARTING_DOLLARS, STARTING_SHARES)
        self.wlock = threading.Lock()
        self.sock = socket.create_connection((HOST, PORT))
        self.send({"name": name, "capital": self.wallet.start})
        threading.Thread(target=self._listen, daemon=True).start()

    def send(self, obj):
        with self.wlock:
            self.sock.sendall((json.dumps(obj) + "\n").encode())

    def order(self, side, price, qty):
        px, qty = int(round(price)), int(qty)
        if not self.wallet.allow(side, px, qty):
            return                       # cannot afford / don't own it, skip
        self.send({"ticker": TICKER, "side": side, "type": LIMIT,
                   "price": px, "volume": qty, "stop": 0})

    def _listen(self):
        try:
            for line in self.sock.makefile("r"):
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if msg.get("type") == "fill":
                    side = BUY if msg["side"] in ("BUY", BUY) else SELL
                    self.wallet.fill(side, int(msg["price"]), int(msg["qty"]))
        except (OSError, ValueError, KeyError):
            pass


def run_noise(c, stop):
    while not stop.is_set():
        time.sleep(random.expovariate(4))                  # ~4 orders/sec
        side = random.choice([BUY, SELL])
        ref = c.tape.snapshot()[0]
        px = ref + 150 if side == BUY else ref - 150       # sweeps several levels
        c.order(side, px, random.randint(10, 100))


def run_informed(c, stop):
    fair = float(START_CENTS)
    while not stop.is_set():
        fair += random.gauss(0, 2.0)                       # private information
        last = c.tape.snapshot()[0]
        if fair > last + 6:
            c.order(BUY, fair, 50)                         # market is too cheap
        elif fair < last - 6:
            c.order(SELL, fair, 50)                        # market is too rich
        time.sleep(0.5)


def vol_bps(samples):
    """Std of 1-second returns, in basis points."""
    rets = [(b - a) / a * 10_000 for a, b in zip(samples, samples[1:]) if a]
    return statistics.pstdev(rets) if len(rets) > 1 else 0.0


def main():
    tape, stop = Tape(), threading.Event()
    try:
        mm = SimMarketMaker(tape)
        noise, informed = Flow("Noise", tape), Flow("Informed", tape)
    except ConnectionRefusedError:
        print(f"Could not connect. Start the exchange first (port {PORT}).")
        return

    threading.Thread(target=mm.run, daemon=True).start()   # your quoting loop
    time.sleep(2)                                          # let quotes land first
    for fn, c in ((run_noise, noise), (run_informed, informed)):
        threading.Thread(target=fn, args=(c, stop), daemon=True).start()

    print(f"Simulating {TICKER} for {DURATION_SECS}s. Every agent starts with "
          f"${STARTING_DOLLARS:,} and {STARTING_SHARES} shares.\n")
    print(f"{'t':>3} {'last':>8} {'trades':>7} {'volume':>8} "
          f"{'MM cash':>12} {'MM shares':>10}")
    samples = [START_CENTS]
    for t in range(1, DURATION_SECS + 1):
        time.sleep(1)
        last, volume, trades = tape.snapshot()
        samples.append(last)
        cash, pos = mm.wallet.snapshot()
        print(f"{t:>3} {last / 100:>8.2f} {trades:>7} {volume:>8} "
              f"{cash / 100:>12,.2f} {pos:>10}")

    stop.set()
    time.sleep(0.3)
    last, volume, trades = tape.snapshot()
    prices = tape.prices or [START_CENTS]
    moves = [b - a for a, b in zip(prices, prices[1:])]
    w = mm.wallet
    cash, pos = w.snapshot()

    print("\n" + "=" * 54)
    print("SUMMARY")
    print("=" * 54)
    print(f"Trades / shares traded   : {trades} / {volume}")
    print(f"Start -> end price       : {START_CENTS / 100:.2f} -> {last / 100:.2f}")
    print(f"Price range              : {min(prices) / 100:.2f} - {max(prices) / 100:.2f}")
    print(f"Volatility (1s returns)  : {vol_bps(samples):.2f} bps")
    print(f"Std trade-to-trade       : "
          f"{(statistics.pstdev(moves) if len(moves) > 1 else 0):.2f} cents")
    print("-" * 54)
    print(f"Market maker fills       : {w.fills} ({w.traded} shares)")
    print(f"Starting cash            : ${w.start / 100:,.2f}")
    print(f"Ending cash              : ${cash / 100:,.2f}")
    print(f"Lowest cash balance      : ${w.low / 100:,.2f}")
    print(f"Ending inventory         : {pos} shares "
          f"(worth ${pos * last / 100:,.2f}, peak {w.max_abs_pos})")
    print(f"Mark-to-market P&L       : ${w.pnl(last) / 100:,.2f}")
    for s in (mm.sock, noise.sock, informed.sock):
        s.close()


if __name__ == "__main__":
    main()
