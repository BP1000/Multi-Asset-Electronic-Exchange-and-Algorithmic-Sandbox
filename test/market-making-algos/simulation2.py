"""
Market maker 2: INVENTORY-SKEW QUOTER (needs no market data).

Idea: always keep exactly one bid and one ask resting. Both quotes are centred
on a reservation price that starts at the anchor and then follows the price of
our own last fill. The reservation price is pushed away from our inventory:

    reservation = last fill price - SKEW * position
    bid = reservation - HALF_SPREAD        ask = reservation + HALF_SPREAD

When we are long, both quotes shift down, so our ask is easier to hit and our
bid is harder to hit. When we are short, they shift up. When a quote trades
completely we post a fresh one on that side. The only inputs are our own fills
and our own position. No order book, no price history, no market data messages.

Every agent starts with $100,000 and 1,000 shares, and can never spend
more cash or sell more shares than it owns (no shorting).

Usage:  python3 skew_quoter_mm.py [seconds]        (default 60)

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

HOST, PORT = "127.0.0.1", 8080
TICKER = "AAPL"
BUY, SELL = 0, 1                  # enum Side { Buy, Ask }
LIMIT = 1                         # enum OrderType { marketOrder, limitOrder }

DURATION_SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 60
STARTING_DOLLARS = 100_000        # cash every agent starts with
STARTING_SHARES = 50000            # shares every agent starts with
START_CENTS = 12000               # prices are integer cents; createNewBook("AAPL", 12000)


class Wallet:
    """Cash and shares. Buys must be fully covered by free cash and sells by
    free shares, so neither cash nor inventory can ever go below zero."""

    def __init__(self, dollars, shares=0):
        self.start = dollars * 100
        self.cash = self.start           # actual cash balance
        self.reserved = 0                # cash committed to resting buys
        self.buys = deque()              # [limit price, shares left], oldest first
        self.start_pos = shares
        self.pos = shares                # shares owned
        self.reserved_shares = 0         # shares committed to resting sells
        self.low = self.start            # lowest balance ever seen
        self.max_abs_pos = abs(shares)
        self.fills = 0
        self.traded = 0
        self.lock = threading.Lock()

    def allow(self, side, px, qty):
        """Reserve cash (buy) or shares (sell). False if not affordable/owned."""
        if qty <= 0:
            return False
        with self.lock:
            if side == BUY:
                cost = px * qty
                if self.cash - self.reserved < cost:
                    return False
                self.reserved += cost
                self.buys.append([px, qty])
                return True
            if self.pos - self.reserved_shares < qty:   # can't sell what we don't own
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
            return self.cash - self.start + (self.pos - self.start_pos) * mark


class Tape:
    """Every trade on the exchange. Each trade has exactly one BUY-side fill,
    so only BUY fills are recorded (no double counting)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.last, self.volume, self.trades, self.prices = START_CENTS, 0, 0, []

    def record(self, px, qty):
        with self.lock:
            self.last = px
            self.volume += qty
            self.trades += 1
            self.prices.append(px)

    def touch(self, px):
        """Update the last price from a SELL-side fill (no volume counted)."""
        with self.lock:
            self.last = px

    def snapshot(self):
        with self.lock:
            return self.last, self.volume, self.trades


class Client:
    """One connection to the exchange, with a $100,000 wallet and 1,000 shares. Subclasses can
    override on_fill(side, price, qty)."""

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
        """Send a limit order. Returns False if a buy is not affordable."""
        px, qty = int(round(price)), int(qty)
        if qty <= 0 or px <= 0 or not self.wallet.allow(side, px, qty):
            return False
        self.send({"ticker": TICKER, "side": side, "type": LIMIT,
                   "price": px, "volume": qty, "stop": 0})
        return True

    def _listen(self):
        try:
            for line in self.sock.makefile("r"):
                try:
                    msg = json.loads(line)
                except ValueError:
                    continue
                if msg.get("type") == "fill" and "price" in msg and "qty" in msg:
                    side = BUY if msg.get("side") in ("BUY", BUY) else SELL
                    px, qty = int(msg["price"]), int(msg["qty"])
                    self.wallet.fill(side, px, qty)
                    if side == BUY:
                        self.tape.record(px, qty)    # one BUY side per trade
                    else:
                        self.tape.touch(px)          # price moves on any fill
                    self.on_fill(side, px, qty)
        except OSError:
            pass

    def on_fill(self, side, px, qty):
        pass




# ---------------- market maker 2 settings ----------------
ANCHOR = START_CENTS      # first reference price, in cents
HALF_SPREAD = 6           # cents from the reservation price to each quote
SIZE = 50                 # shares per quote
SKEW_PER_SHARE = 0.05     # cents the quotes shift per share of inventory
MAX_POSITION = 300        # stop buying once we hold this many shares above the start


class MarketMaker(Client):
    NAME = "Inventory-skew quoter"

    def __init__(self, tape, stop):
        super().__init__("SkewQuoter", tape)
        self.stop = stop
        self.ref = float(ANCHOR)               # price of our last fill
        self.live = {BUY: 0, SELL: 0}          # shares still resting on each side
        self.state = threading.Lock()

    def quote(self, side):
        """Post one quote on this side if we have none resting."""
        with self.state:
            if self.live[side] > 0 or self.stop.is_set():
                return
            pos = self.wallet.snapshot()[1] - STARTING_SHARES   # net vs. start
            if side == BUY and pos >= MAX_POSITION:
                return
            reservation = self.ref - SKEW_PER_SHARE * pos
            px = reservation - HALF_SPREAD if side == BUY else reservation + HALF_SPREAD
            if self.order(side, px, SIZE):
                self.live[side] = SIZE

    def start(self):
        self.quote(BUY)
        self.quote(SELL)

    def tick(self):                            # retry a side that could not quote
        self.quote(BUY)
        self.quote(SELL)

    def on_fill(self, side, px, qty):
        with self.state:
            self.ref = float(px)               # follow the market through our trades
            self.live[side] = max(0, self.live[side] - qty)
        self.quote(side)                       # replace what traded


# ---------------- order flow the market maker trades against ----------------
def run_noise(c, stop):
    """Random buys and sells that cross the spread (Poisson arrivals)."""
    while not stop.is_set():
        time.sleep(random.expovariate(3))                  # ~3 orders/sec
        side = random.choice([BUY, SELL])
        ref = c.tape.snapshot()[0]
        px = ref + 40 if side == BUY else ref - 40
        c.order(side, px, random.randint(10, 60))


def run_informed(c, stop):
    """Hidden true value that random-walks; trades when the market is off it."""
    fair = float(START_CENTS)
    while not stop.is_set():
        fair += random.gauss(0, 2.0)
        last = c.tape.snapshot()[0]
        if fair > last + 6:
            c.order(BUY, fair, 30)
        elif fair < last - 6:
            c.order(SELL, fair, 30)
        time.sleep(0.5)


def vol_bps(samples):
    """Std of 1-second returns, in basis points."""
    rets = [(b - a) / a * 10_000 for a, b in zip(samples, samples[1:]) if a]
    return statistics.pstdev(rets) if len(rets) > 1 else 0.0


def main():
    tape, stop = Tape(), threading.Event()
    try:
        mm = MarketMaker(tape, stop)
        noise, informed = Client("Noise", tape), Client("Informed", tape)
    except ConnectionRefusedError:
        print(f"Could not connect. Start the exchange first (port {PORT}).")
        return

    mm.start()
    time.sleep(1)                                          # let the quotes land first
    for fn, c in ((run_noise, noise), (run_informed, informed)):
        threading.Thread(target=fn, args=(c, stop), daemon=True).start()

    print(f"{MarketMaker.NAME}: simulating {TICKER} for {DURATION_SECS}s. "
          f"Every agent starts with ${STARTING_DOLLARS:,} and {STARTING_SHARES} shares.\n")
    print(f"{'t':>3} {'last':>8} {'trades':>7} {'volume':>8} "
          f"{'MM cash':>12} {'MM shares':>10}")
    samples = [START_CENTS]
    for t in range(1, DURATION_SECS + 1):
        time.sleep(1)
        mm.tick()
        last, volume, trades = tape.snapshot()
        samples.append(last)
        cash, pos = mm.wallet.snapshot()
        print(f"{t:>3} {last / 100:>8.2f} {trades:>7} {volume:>8} "
              f"{cash / 100:>12,.2f} {pos:>10}")

    stop.set()
    time.sleep(0.3)
    last, volume, trades = tape.snapshot()
    prices = tape.prices or [START_CENTS]
    w = mm.wallet
    cash, pos = w.snapshot()
    print("\n" + "=" * 54)
    print("SUMMARY")
    print("=" * 54)
    print(f"Trades / shares traded   : {trades} / {volume}")
    print(f"Start -> end price       : {START_CENTS / 100:.2f} -> {last / 100:.2f}")
    print(f"Price range              : {min(prices) / 100:.2f} - {max(prices) / 100:.2f}")
    print(f"Volatility (1s returns)  : {vol_bps(samples):.2f} bps")
    print("-" * 54)
    print(f"Market maker fills       : {w.fills} ({w.traded} shares)")
    print(f"Starting cash            : ${w.start / 100:,.2f}")
    print(f"Ending cash              : ${cash / 100:,.2f}")
    print(f"Lowest cash balance      : ${w.low / 100:,.2f}")
    print(f"Inventory                : {pos} shares "
          f"(worth ${pos * last / 100:,.2f}, peak {w.max_abs_pos})")
    print(f"Market maker P&L (MTM)   : ${w.pnl(last) / 100:,.2f}")
    print(f"Noise P&L                : ${noise.wallet.pnl(last) / 100:,.2f} "
          f"(lowest cash ${noise.wallet.low / 100:,.2f})")
    print(f"Informed P&L             : ${informed.wallet.pnl(last) / 100:,.2f} "
          f"(lowest cash ${informed.wallet.low / 100:,.2f})")
    for s in (mm.sock, noise.sock, informed.sock):
        s.close()


if __name__ == "__main__":
    main()
