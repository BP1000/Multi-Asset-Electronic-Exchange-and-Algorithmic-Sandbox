"""
Scalper: a TRADING algorithm (it takes liquidity, it never quotes).

It enters a position with a marketable limit order, then immediately rests a
take-profit order a fixed number of cents away:

    bought 20 @ 119.95  ->  rest a sell of 20 @ 119.95 + TARGET
    sold   20 @ 120.05  ->  rest a buy  of 20 @ 120.05 - TARGET

When to enter:
  - if the server sends market_data: mean reversion against a fixed anchor
    price. Buy when the mid is more than ENTRY_EDGE below the anchor, sell when
    it is more than ENTRY_EDGE above it.
  - if it has no market_data: enter at random times, leaning against its own
    inventory so it does not drift far one way.

It stores no price history. Its only state is its position, the anchor, and the
latest mid price. It trades against whatever liquidity is resting on the
exchange (your market makers), so start those first.

It starts with $100,000 and can never spend more than it has.

Usage:  python3 scalper_trader.py          (stop with Ctrl+C)
Needs the server to send fill messages:
  {"type":"fill","side":"BUY"|"SELL","price":<int cents>,"qty":<int>}
"""
import json
import random
import socket
import threading
import time
from collections import deque

HOST, PORT = "127.0.0.1", 8080
TICKER = "AAPL"
BUY, SELL = 0, 1                  # enum Side { Buy, Ask }
LIMIT = 1                         # enum OrderType { marketOrder, limitOrder }

STARTING_DOLLARS = 100_000
ANCHOR = 12000                    # fixed fair value belief, in cents
ENTRY_EDGE = 3                    # cents from the anchor before it trades (with market_data)
TARGET = 10                       # cents of profit each exit order aims for
CROSS = 30                        # entry price is this far through the reference price
SIZE = 20                         # shares per trade
MAX_POSITION = 200                # no new entries that add to inventory past this
MEAN_GAP_SECS = 1.0               # average time between entries


class Wallet:
    """Cash and inventory in cents. Buys must be fully covered by free cash,
    so the balance can never go below zero."""

    def __init__(self, dollars):
        self.start = dollars * 100
        self.cash = self.start
        self.reserved = 0                # cash committed to resting buys
        self.buys = deque()              # [limit price, shares left], oldest first
        self.pos = 0
        self.low = self.start
        self.lock = threading.Lock()

    def allow(self, side, px, qty):
        if side != BUY:
            return True
        with self.lock:
            cost = px * qty
            if self.cash - self.reserved < cost:
                return False
            self.reserved += cost
            self.buys.append([px, qty])
            return True

    def fill(self, side, px, qty):
        with self.lock:
            if side == BUY:
                self.cash -= px * qty
                self.pos += qty
                left = qty
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
            self.low = min(self.low, self.cash)

    def snapshot(self):
        with self.lock:
            return self.cash, self.pos

    def pnl(self, mark):
        with self.lock:
            return self.cash - self.start + self.pos * mark


class Scalper:
    def __init__(self, name="Scalper"):
        self.wallet = Wallet(STARTING_DOLLARS)
        self.mid = None                       # latest mid from market_data
        self.last_px = None                   # price of our latest fill
        self.entries = 0
        self.exits = 0
        # shares of entry orders sent but not yet filled, per side. A fill on a
        # side with pending shares counts as an entry fill, otherwise as an exit.
        self.pending = {BUY: 0, SELL: 0}
        self.state = threading.Lock()
        self.wlock = threading.Lock()
        self.sock = socket.create_connection((HOST, PORT))
        self.send({"name": name, "capital": self.wallet.start})
        threading.Thread(target=self._listen, daemon=True).start()

    # ---------- networking ----------
    def send(self, obj):
        with self.wlock:
            self.sock.sendall((json.dumps(obj) + "\n").encode())

    def order(self, side, price, qty):
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
                kind = msg.get("type")
                if kind == "market_data":
                    bid, ask = msg.get("bid"), msg.get("ask")
                    if bid is not None and ask is not None:
                        with self.state:
                            self.mid = (bid + ask) / 2
                elif kind == "fill" and "price" in msg and "qty" in msg:
                    side = BUY if msg.get("side") in ("BUY", BUY) else SELL
                    self._on_fill(side, int(msg["price"]), int(msg["qty"]))
        except OSError:
            pass

    # ---------- strategy ----------
    def _on_fill(self, side, px, qty):
        self.wallet.fill(side, px, qty)
        with self.state:
            self.last_px = px
            entry_qty = min(qty, self.pending[side])
            self.pending[side] -= entry_qty
            if entry_qty:
                self.entries += 1
            else:
                self.exits += 1
        if entry_qty:                         # entry filled: rest the take-profit
            if side == BUY:
                self.order(SELL, px + TARGET, entry_qty)
            else:
                self.order(BUY, px - TARGET, entry_qty)

    def reference(self):
        with self.state:
            return self.mid if self.mid is not None else (self.last_px or ANCHOR)

    def choose_side(self):
        """Returns (side or None, mode)."""
        pos = self.wallet.snapshot()[1]
        with self.state:
            mid = self.mid
        if mid is not None:                   # mean reversion against the anchor
            mode = "anchor"
            if mid < ANCHOR - ENTRY_EDGE:
                side = BUY
            elif mid > ANCHOR + ENTRY_EDGE:
                side = SELL
            else:
                return None, mode
        else:                                 # no data: random, leaning against inventory
            mode = "random"
            p_buy = min(0.9, max(0.1, 0.5 - pos / (4 * MAX_POSITION)))
            side = BUY if random.random() < p_buy else SELL
        if side == BUY and pos >= MAX_POSITION:
            return None, mode
        if side == SELL and pos <= -MAX_POSITION:
            return None, mode
        return side, mode

    def enter(self):
        side, mode = self.choose_side()
        if side is None:
            return mode
        ref = self.reference()
        px = ref + CROSS if side == BUY else ref - CROSS
        if self.order(side, px, SIZE):
            with self.state:
                self.pending[side] += SIZE
        return mode

    def run(self):
        print(f"[Scalper] starting with ${STARTING_DOLLARS:,}. Stop with Ctrl+C.")
        next_status = time.time() + 2
        mode = "random"
        while True:
            time.sleep(random.expovariate(1 / MEAN_GAP_SECS))
            mode = self.enter()
            if time.time() >= next_status:
                next_status = time.time() + 2
                cash, pos = self.wallet.snapshot()
                mark = self.reference()
                print(f"mode={mode:<6} price={mark / 100:7.2f}  pos={pos:5d}  "
                      f"cash={cash / 100:12,.2f}  P&L={self.wallet.pnl(mark) / 100:9,.2f}  "
                      f"entries={self.entries}  exits={self.exits}")


if __name__ == "__main__":
    try:
        Scalper().run()
    except ConnectionRefusedError:
        print(f"Could not connect. Start the exchange first (port {PORT}).")
    except KeyboardInterrupt:
        print("\nStopped.")
