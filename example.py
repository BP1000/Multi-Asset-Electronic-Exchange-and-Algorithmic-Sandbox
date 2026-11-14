import json
import socket
import threading
import time
from collections import deque

# ---------------- Configuration ----------------
HOST, PORT = "127.0.0.1", 8080
TICKER = "AAPL"

PRICE_SCALE = 100        # server prices are integers: 100 = cents
START_PRICE = 120.0      # must match createNewBook("AAPL", ...) / PRICE_SCALE
HALF_SPREAD = 0.05       # dollars from mid on each side
QUOTE_SIZE = 10
MAX_POSITION = 200
SKEW_PER_SHARE = 0.001   # dollars of quote shift per share of inventory
REFRESH_SECS = 0.5
TICK = 0.01

# Flip to True once the server handles {"action": "cancel"}.
CANCEL_SUPPORTED = False
MAX_RESTING_QUOTES = 2   # when cancel is unsupported, stop posting beyond this

# Enum values: must match orderbook.hpp
BUY, SELL = 0, 1                      # enum Side { Buy, Ask }
MARKET_ORDER, LIMIT_ORDER = 0, 1      # enum OrderType { marketOrder, limitOrder }


def to_ticks(price):
    return int(round(price * PRICE_SCALE))


class MarketMaker:
    def __init__(self, name="MarketMaker", capital=1_000_000):
        self.name = name
        self.cash = capital * PRICE_SCALE      # server capital is in ticks too
        self.position = 0
        self.bid = None                        # dollars, from market_data
        self.ask = None
        self.last = None
        self.live_orders = []                  # server-assigned ids of resting quotes
        self.pending = deque()                 # orders sent, waiting for their ack
        self.connected = True

        self.write_lock = threading.Lock()
        self.state_lock = threading.Lock()

        self.sock = socket.create_connection((HOST, PORT))
        self._send({"name": name, "capital": self.cash})      # login
        threading.Thread(target=self._listen, daemon=True).start()

    # ---------- networking ----------
    def _send(self, obj):
        data = (json.dumps(obj) + "\n").encode()
        with self.write_lock:
            self.sock.sendall(data)

    def _listen(self):
        try:
            for line in self.sock.makefile("r"):
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                with self.state_lock:
                    kind = msg.get("type")
                    if kind == "ack":
                        # Server processes our orders in order, so acks match FIFO.
                        if self.pending:
                            self.pending.popleft()
                        if msg.get("id"):
                            self.live_orders.append(msg["id"])
                    elif kind == "market_data":
                        self.bid = msg.get("bid")
                        self.ask = msg.get("ask")
                        self.last = msg.get("last")
                    elif kind == "fill":
                        self.cash = msg["cash"]
                        self.position = msg["position"]
                        if self.live_orders and not CANCEL_SUPPORTED:
                            self.live_orders.pop(0)    # a quote traded, free a slot
        finally:
            self.connected = False

    def place(self, side, qty, price):
        with self.state_lock:
            self.pending.append(side)
        msg = {
            "ticker": TICKER,
            "side": side,                       # int, not string
            "type": LIMIT_ORDER,
            "volume": int(qty),
            "price": to_ticks(price),           # int ticks, not float
            "stop": 0,
        }
        self._send(msg)
        print(msg)

    def cancel(self, oid):
        # Needs server support; harmless to call while CANCEL_SUPPORTED is False.
        if CANCEL_SUPPORTED:
            self._send({"action": "cancel", "ticker": TICKER, "id": oid})

    # ---------- strategy ----------
    def fair_price(self):
        with self.state_lock:
            if self.bid and self.ask:
                return (self.bid + self.ask) / 2
            return self.last or START_PRICE

    def compute_quotes(self):
        mid = self.fair_price()
        with self.state_lock:
            pos = self.position

        skew = -SKEW_PER_SHARE * pos
        bid = mid - HALF_SPREAD + skew
        ask = mid + HALF_SPREAD + skew
        if ask - bid < TICK:
            ask = bid + TICK
        return bid, ask, pos

    def requote(self):
        if CANCEL_SUPPORTED:
            with self.state_lock:
                old, self.live_orders = self.live_orders, []
            for oid in old:
                self.cancel(oid)
        else:
            # No cancel: don't let stale quotes pile up in the book.
            with self.state_lock:
                outstanding = len(self.live_orders) + len(self.pending)
            if outstanding >= MAX_RESTING_QUOTES:
                return

        bid, ask, pos = self.compute_quotes()
        if pos < MAX_POSITION:
            self.place(BUY, QUOTE_SIZE, bid)
        if pos > -MAX_POSITION:
            self.place(SELL, QUOTE_SIZE, ask)

    def run(self):
        print(f"[{self.name}] quoting {TICKER}")
        while self.connected:
            self.requote()
            with self.state_lock:
                mark = self.last or START_PRICE
                pnl = (self.cash + self.position * mark * PRICE_SCALE) / PRICE_SCALE
                #print(f"pos={self.position:5d}  cash={self.cash / PRICE_SCALE:12.2f}"
                #      f"  pnl~{pnl:12.2f}  resting={len(self.live_orders)}")
            time.sleep(REFRESH_SECS)
        print("Disconnected from exchange.")


if __name__ == "__main__":
    MarketMaker().run()
