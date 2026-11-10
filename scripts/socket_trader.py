#!/usr/bin/env python3
"""Send AAPL limit orders from current quotes read as newline-delimited JSON."""
import argparse
import json
import socket
import statistics
import sys
from collections import deque
from dataclasses import dataclass
from fractions import Fraction
from math import ceil

HOST, PORT = "127.0.0.1", 8080
MAKERS = ("fixed_spread", "inventory_aware", "volatility_adaptive")
TRADERS = ("momentum", "mean_reversion", "order_book_imbalance", "twap")


@dataclass(frozen=True)
class Quote:
    bid: int
    ask: int
    bid_size: int = 0
    ask_size: int = 0

    def __post_init__(self):
        if any(type(value) is not int for value in (self.bid, self.ask, self.bid_size, self.ask_size)):
            raise ValueError("quote prices and sizes must be integers")
        if not 0 < self.bid < self.ask or min(self.bid_size, self.ask_size) < 0:
            raise ValueError("expected 0 < bid < ask and nonnegative sizes")

    @property
    def midpoint(self):
        return Fraction(self.bid + self.ask, 2)


class Trader:
    def __init__(self, strategy, trader_id, quantity, shares, interval, slices):
        self.strategy = strategy
        self.trader_id = trader_id
        self.quantity = quantity
        self.inventory = shares
        self.cash = (1_000_000 if strategy in MAKERS else 50_000) * 100
        self.interval = interval
        self.slices = slices
        self.previous_mid = None
        self.midpoints = deque(maxlen=20)
        self.changes = deque(maxlen=20)
        self.quoted_sides = set()

    def order(self, side, price, quantity):
        # Reserve the full limit notional. Without fill reports, never assume
        # a sale freed cash or a purchase made shares available.
        if side == "Buy":
            quantity = min(quantity, self.cash // price)
            self.cash -= price * quantity
        else:
            quantity = min(quantity, self.inventory)
            self.inventory -= quantity
        if quantity:
            return {"action": "place order", "trader id": self.trader_id,
                    "name": "AAPL", "price": price, "volume": quantity,
                    "stop": 0, "Side": side, "type": "limitOrder"}
        return None

    def on_quote(self, quote, tick):
        mid = quote.midpoint
        previous = self.previous_mid
        self.previous_mid = mid
        self.midpoints.append(mid)
        if previous is not None:
            self.changes.append(mid - previous)

        if self.strategy in MAKERS:
            width = 2
            if self.strategy == "volatility_adaptive":
                if len(self.changes) < 2:
                    return
                width = max(width, ceil(2 * statistics.pstdev(self.changes)))
            skew = 0 if self.strategy == "fixed_spread" else self.inventory // 10
            bid = max(1, min(quote.ask - 1, int(mid) - skew - width))
            ask = max(quote.bid + 1, bid + 1, ceil(mid) - skew + width)
            for side, price in (("Buy", bid), ("Ask", ask)):
                # Only one order per side until the exchange supports replacement.
                if side not in self.quoted_sides:
                    order = self.order(side, price, self.quantity)
                    if order:
                        self.quoted_sides.add(side)
                        yield order
            return

        direction = 0
        quantity = self.quantity
        if self.strategy == "momentum" and previous is not None:
            direction = mid - previous
        elif self.strategy == "mean_reversion" and len(self.midpoints) > 1:
            gap = statistics.mean(self.midpoints) - mid
            if abs(gap) >= quote.ask - quote.bid:
                direction = gap
        elif self.strategy == "order_book_imbalance":
            total = quote.bid_size + quote.ask_size
            imbalance = quote.bid_size - quote.ask_size
            if total and 3 * abs(imbalance) >= total:
                direction = imbalance
        elif self.strategy == "twap":
            slot = tick // self.interval
            if tick % self.interval == 0 and slot < self.slices:
                base, remainder = divmod(self.quantity, self.slices)
                quantity = base + (slot < remainder)
                direction = 1
        if direction and quantity:
            side, price = ("Buy", quote.ask) if direction > 0 else ("Ask", quote.bid)
            order = self.order(side, price, quantity)
            if order:
                yield order


def run(trader, quotes, connection):
    for tick, line in enumerate(quotes):
        quote = Quote(**json.loads(line))
        for order in trader.on_quote(quote, tick):
            connection.sendall((json.dumps(order) + "\n").encode("utf-8"))


def positive_int(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("strategy", choices=MAKERS + TRADERS)
    parser.add_argument("--trader-id", type=positive_int, required=True)
    parser.add_argument("--quantity", type=positive_int, default=10,
                        help="shares per order, or total shares for TWAP")
    parser.add_argument("--shares", type=int, default=0,
                        help="AAPL shares already credited by the exchange")
    parser.add_argument("--interval", type=positive_int, default=10,
                        help="quote updates between TWAP slices")
    parser.add_argument("--slices", type=positive_int, default=5)
    args = parser.parse_args()
    if args.shares < 0:
        parser.error("--shares must be nonnegative")
    trader = Trader(args.strategy, args.trader_id, args.quantity, args.shares,
                    args.interval, args.slices)
    try:
        with socket.create_connection((HOST, PORT), timeout=5) as connection:
            run(trader, sys.stdin, connection)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"socket trader: {error}\n")


if __name__ == "__main__":
    main()
