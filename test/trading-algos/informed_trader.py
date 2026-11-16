"""Informed trader: knows a hidden 'true value' that random-walks, and trades
toward it. Buys only at or below true value, sells only at or above it, so it
takes liquidity exactly when a quote is mispriced. It needs no market data and
no history: the true value is its own current state, not a record of the past."""
import json, random, socket, threading, time

HOST, PORT = "127.0.0.1", 8080
TICKER = "AAPL"
BUY, SELL = 0, 1
LIMIT = 1

START_PRICE = 12000          # cents, must match createNewBook
STEP_STD = 2.0               # cents the true value moves each step
EDGE_TICKS = 3               # only trade when mispriced by at least this much
SIZE = 5
INTERVAL_SECS = 0.5
MAX_ORDERS = 300             # budget, because there is no cancel yet


class Agent:
    def __init__(self, name="Informed", capital=100_000_000):
        self.fair = float(START_PRICE)
        self.sent = 0
        self.sock = socket.create_connection((HOST, PORT))
        self.lock = threading.Lock()
        self.send({"name": name, "capital": capital})
        threading.Thread(target=self.listen, daemon=True).start()

    def send(self, obj):
        with self.lock:
            self.sock.sendall((json.dumps(obj) + "\n").encode())

    def listen(self):
        for line in self.sock.makefile("r"):
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get("type") == "fill":
                print("FILL", msg)

    def order(self, side, px):
        self.send({"ticker": TICKER, "side": side, "type": LIMIT,
                   "price": int(round(px)), "volume": SIZE, "stop": 0})
        self.sent += 1

    def run(self):
        print("[Informed] starting true value", self.fair)
        while self.sent < MAX_ORDERS:
            self.fair += random.gauss(0, STEP_STD)           # private information
            # A buy limit at (fair - edge) only fills against asks at or below
            # that price. A sell limit at (fair + edge) only fills against bids
            # at or above it. So it trades only when a quote is mispriced.
            self.order(BUY, self.fair - EDGE_TICKS)
            self.order(SELL, self.fair + EDGE_TICKS)
            print(f"true value {self.fair:.1f}  bid {self.fair - EDGE_TICKS:.0f}"
                  f"  ask {self.fair + EDGE_TICKS:.0f}")
            time.sleep(INTERVAL_SECS)
        print("[Informed] order budget used up")


if __name__ == "__main__":
    Agent().run()
