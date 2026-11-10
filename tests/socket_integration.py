"""Exercise every strategy against a TCP listener and check the wire contract."""
import json
import socket
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "scripts"))
from socket_trader import MAKERS, TRADERS, Quote, Trader

quotes = [
    {"bid": bid, "ask": bid + 4, "bid_size": 300, "ask_size": 50}
    for bid in (10000, 10010, 10000, 9980, 9990)
]
payload = "".join(json.dumps(quote) + "\n" for quote in quotes)
expected = {
    "fixed_spread": [("Buy", 10000, 10), ("Ask", 10004, 10)],
    "inventory_aware": [("Buy", 9998, 10), ("Ask", 10002, 10)],
    "volatility_adaptive": [("Buy", 9980, 10), ("Ask", 10020, 10)],
    "momentum": [("Buy", 10014, 10), ("Ask", 10000, 10),
                 ("Ask", 9980, 10), ("Buy", 9994, 10)],
    "mean_reversion": [("Ask", 10010, 10), ("Buy", 9984, 10), ("Buy", 9994, 10)],
    "order_book_imbalance": [("Buy", quote["ask"], 10) for quote in quotes],
    "twap": [("Buy", 10004, 4), ("Buy", 10004, 3), ("Buy", 9994, 3)],
}
for strategy in MAKERS + TRADERS:
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen()
        server.settimeout(5)
        command = (
            "import socket_trader; "
            f"socket_trader.PORT={server.getsockname()[1]}; socket_trader.main()"
        )
        result = subprocess.run(
            [sys.executable, "-c", command, strategy, "--trader-id", "42",
             "--shares", "20", "--interval", "2", "--slices", "3"],
            cwd=root / "scripts", input=payload, capture_output=True,
            text=True, timeout=5, check=True,
        )
        assert not result.stderr
        connection, _ = server.accept()
        with connection, connection.makefile("r", encoding="utf-8") as stream:
            connection.settimeout(5)
            orders = [json.loads(line) for line in stream]
    actual = []
    for order in orders:
        assert order == {
            "action": "place order", "trader id": 42, "name": "AAPL",
            "price": order["price"], "volume": order["volume"],
            "stop": 0, "Side": order["Side"], "type": "limitOrder",
        }
        actual.append((order["Side"], order["price"], order["volume"]))
    assert actual == expected[strategy], (strategy, actual)

for strategy, dollars in (("fixed_spread", 1_000_000), ("twap", 50_000)):
    trader = Trader(strategy, 42, 100_000, 0, 1, 1)
    orders = list(trader.on_quote(Quote(10000, 10002), 0))
    assert len(orders) == 1
    assert orders[0]["Side"] == "Buy"
    assert orders[0]["volume"] == dollars * 100 // orders[0]["price"]
    assert trader.order("Buy", orders[0]["price"], 1) is None
    assert trader.order("Ask", 10000, 1) is None
print("All seven strategies passed TCP, order format, and budget checks.")
