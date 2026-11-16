"""Noise trader: random buys and sells arriving as a Poisson process.
Memoryless: each order depends only on a coin flip and the reference price."""
import json, random, socket, threading, time

HOST, PORT = "127.0.0.1", 8080
TICKER = "AAPL"
BUY, SELL = 0, 1                 # enum Side { Buy, Ask }
LIMIT = 1                        # enum OrderType { marketOrder, limitOrder }

START_PRICE = 12000              # cents; match createNewBook("AAPL", 12000)
MEAN_GAP_SECS = 0.7              # average time between orders
MAX_SIZE = 10
CROSS_TICKS = 10                 # price this far through the reference so it fills


class Agent:
    def __init__(self, name="Noise", capital=100_000_000):
        self.mid = None          # filled only if the server sends market_data
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
            if msg.get("type") == "market_data" and msg.get("bid") and msg.get("ask"):
                self.mid = (msg["bid"] + msg["ask"]) / 2 * 100   # dollars -> cents
            elif msg.get("type") == "fill":
                print("FILL", msg)

    def run(self):
        print("[Noise] trading", TICKER)
        while True:
            time.sleep(random.expovariate(1 / MEAN_GAP_SECS))
            side = random.choice([BUY, SELL])
            ref = self.mid or START_PRICE
            px = ref + CROSS_TICKS if side == BUY else ref - CROSS_TICKS
            qty = random.randint(1, MAX_SIZE)
            self.send({"ticker": TICKER, "side": side, "type": LIMIT,
                       "price": int(round(px)), "volume": qty, "stop": 0})
            print(f"SENT {'BUY' if side == BUY else 'SELL'} {qty} @ {int(px)}")


if __name__ == "__main__":
    Agent().run()
