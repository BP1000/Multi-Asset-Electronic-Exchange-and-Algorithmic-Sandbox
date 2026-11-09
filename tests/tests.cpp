#include "sandbox/strategies.hpp"
#include <functional>
#include <iostream>
#include <random>
using namespace sandbox;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void invariant(const Exchange& e) {
  std::map<std::string, int> total;
  std::int64_t cash = 0, fees = 0;
  for (const auto& [id, a] : e.accounts()) {
    (void)id;
    cash += a.cash - 10'000'000; fees += a.fees;
    for (const auto& [symbol, q] : a.positions) total[symbol] += q;
  }
  check(cash + fees == 0, "cash plus fees conserved");
  for (const auto& [symbol, q] : total) { (void)symbol; check(q == 0, "shares conserved"); }
  std::size_t indexed = 0;
  for (const auto& [symbol, book] : e.books()) {
    const auto bid = e.best(symbol, Side::Buy), ask = e.best(symbol, Side::Sell);
    check(!bid || !ask || *bid < *ask, "uncrossed book");
    for (auto side : {Side::Buy, Side::Sell}) {
      const auto& levels = side == Side::Buy ? book.bids : book.asks;
      for (const auto& [price, queue] : levels) {
        check(!queue.empty(), "nonempty level");
        for (auto id : queue) {
          const auto& o = e.orders().at(id);
          check(o.quantity > 0 && o.price == price && o.symbol == symbol && o.side == side, "consistent order index");
          ++indexed;
        }
      }
    }
  }
  check(indexed == e.orders().size(), "no orphan orders");
}
void matching() {
  Exchange e;
  const auto a = e.submit(2, "A", Side::Sell, 5, 101);
  const auto b = e.submit(3, "A", Side::Sell, 8, 101);
  e.submit(4, "A", Side::Sell, 10, 102);
  const auto fill = e.submit(1, "A", Side::Buy, 10, 101);
  check(fill.filled == 10 && fill.resting == 0, "limit fill");
  check(e.trades().size() == 2 && e.trades()[0].maker == a.id && e.trades()[1].maker == b.id, "FIFO");
  check(e.orders().at(b.id).quantity == 3, "partial fill");
  const auto no_cross = e.submit(5, "A", Side::Buy, 1, 100);
  check(no_cross.filled == 0 && no_cross.resting == 1, "respect limit");
  const auto sweep = e.submit(6, "A", Side::Buy, 50);
  check(sweep.filled == 13 && sweep.resting == 0, "market remainder expires");
  check(e.trades().back().price == 102, "sweep ascending asks at maker price");
  check(e.account(1).cash == 10'000'000 - 1010 - 10, "cash and per-share fees");
  check(!e.cancel(a.id) && e.cancel(no_cross.id), "cancellation");
  invariant(e);
  Exchange bids;
  bids.submit(2, "A", Side::Buy, 4, 99);
  bids.submit(3, "A", Side::Buy, 3, 100);
  bids.submit(1, "A", Side::Sell, 5, 99);
  check(bids.trades()[0].price == 100 && bids.trades()[1].price == 99, "descending bids");
  invariant(bids);
}
void modifications() {
  Exchange e;
  const auto a = e.submit(1, "A", Side::Buy, 5, 100);
  const auto b = e.submit(2, "A", Side::Buy, 5, 100);
  const auto replacement = e.modify(a.id, 100, 3);
  check(replacement.id != a.id && replacement.resting == 3, "fresh replacement");
  e.submit(3, "A", Side::Sell, 6);
  check(e.trades()[0].maker == b.id && e.orders().at(replacement.id).quantity == 2, "replacement loses priority");
  bool rejected = false;
  try { e.modify(replacement.id, 0, 1); } catch (const std::invalid_argument&) { rejected = true; }
  check(rejected && e.orders().at(replacement.id).quantity == 2, "invalid edit preserves order");
  e.submit(1, "A", Side::Sell, 2, 100);
  check(e.position(1, "A") == 1 && e.trades().size() == 2, "self-cross cancels resting order");
  check(!e.orders().contains(replacement.id), "no stale self-order");
  invariant(e);
}
void random_operations() {
  Exchange e;
  std::mt19937 rng(123);
  for (int i = 0; i < 5000; ++i) {
    const int action = static_cast<int>(rng() % 5);
    if (action >= 3 && !e.orders().empty()) {
      std::vector<OrderId> ids;
      for (const auto& [id, order] : e.orders()) { (void)order; ids.push_back(id); }
      std::sort(ids.begin(), ids.end());
      const auto id = ids[rng() % ids.size()];
      if (action == 3) e.cancel(id);
      else e.modify(id, 90 + rng() % 21, 1 + rng() % 20);
    } else {
      const int trader = 1 + rng() % 5;
      const std::string symbol = rng() % 2 ? "A" : "B";
      const Side side = rng() % 2 ? Side::Buy : Side::Sell;
      e.submit(trader, symbol, side, 1 + rng() % 20,
               action == 0 ? std::nullopt : std::optional<Price>(90 + rng() % 21));
    }
    invariant(e);
  }
}
void liquid(Exchange& e) {
  e.cancel_all(2);
  for (auto symbol : {"A", "B"}) {
    e.submit(2, symbol, Side::Buy, 1000, 9900);
    e.submit(2, symbol, Side::Sell, 1000, 10100);
  }
}
std::pair<Price, Price> quotes(const Exchange& e) {
  Price bid = 0, ask = 0;
  for (const auto& [id, order] : e.orders()) if (order.trader == 1) {
    (void)id;
    (order.side == Side::Buy ? bid : ask) = order.price;
  }
  return {bid, ask};
}
void makers() {
  std::map<std::string, std::vector<double>> h{{"A", std::vector<double>(30, 10000)}, {"B", std::vector<double>(30, 10000)}};
  Exchange fixed;
  Context c{fixed, 1, 0, 100, h};
  MarketMaker baseline(MakerMode::Fixed);
  baseline.step(c);
  check(quotes(fixed) == std::pair<Price, Price>{9998, 10002}, "fixed symmetric quotes");
  fixed.submit(2, "A", Side::Sell, 5);
  MarketMaker inventory(MakerMode::Inventory);
  inventory.step(c);
  check(quotes(fixed).first < 9998 && quotes(fixed).second <= 10002, "long inventory shifts quotes down");
  for (int i = 0; i < 30; ++i) h["A"][i] = 10000 + (i % 2 ? 20 : -20);
  MarketMaker adaptive(MakerMode::Volatility);
  adaptive.step(c);
  check(quotes(fixed).second - quotes(fixed).first > 4, "volatility widens quotes");
  for (auto mode : {MakerMode::Fixed, MakerMode::Inventory, MakerMode::Volatility}) {
    Exchange e;
    MarketMaker maker(mode);
    Context cx{e, 1, 0, 100, h};
    for (int t = 0; t < 100; ++t) {
      maker.step(cx);
      e.submit(2, "A", Side::Sell, 100);
      check(std::abs(e.position(1, "A")) <= 40, "maker long cap");
    }
    for (int t = 0; t < 100; ++t) {
      maker.step(cx);
      e.submit(2, "A", Side::Buy, 100);
      check(std::abs(e.position(1, "A")) <= 40, "maker short cap");
    }
    invariant(e);
  }
}
void directional() {
  Exchange e;
  liquid(e);
  std::map<std::string, std::vector<double>> h{{"A", {}}, {"B", {}}};
  Context c{e, 1, 0, 100, h};
  Momentum momentum;
  h["A"] = {10000}; momentum.step(c);
  check(e.position(1, "A") == 0, "momentum warmup");
  for (int i = 0; i < 30; ++i) h["A"].push_back(10000 + i * 2);
  momentum.step(c);
  check(e.position(1, "A") == 20, "momentum uptrend");
  h["A"].clear();
  for (int i = 0; i < 30; ++i) h["A"].push_back(10000 - i * 2);
  momentum.step(c);
  check(e.position(1, "A") == -20, "momentum downtrend");
  MeanReversion mean;
  h["A"] = std::vector<double>(29, 10000); h["A"].push_back(10030);
  mean.step(c); check(e.position(1, "A") == -20, "sell high deviation");
  h["A"].back() = 9970;
  mean.step(c); check(e.position(1, "A") == 20, "buy low deviation");
  h["A"] = std::vector<double>(30, 10000);
  mean.step(c); check(e.position(1, "A") == 0, "flat signal exits");
  PairsTrading pairs;
  h["B"] = std::vector<double>(30, 10000);
  h["A"].back() = 10050;
  pairs.step(c);
  check(e.position(1, "A") == -20 && e.position(1, "B") == 20, "pairs rich A");
  h["A"].back() = 9950; pairs.step(c);
  check(e.position(1, "A") == 20 && e.position(1, "B") == -20, "pairs cheap A");
  h["A"].back() = 10000; pairs.step(c);
  check(e.position(1, "A") == 0 && e.position(1, "B") == 0, "pairs exits");
  invariant(e);
}
void twap() {
  Exchange e;
  std::map<std::string, std::vector<double>> h{{"A", {10000}}};
  Context c{e, 1, 0, 25, h};
  Twap strategy(100, 10);
  strategy.step(c); // no liquidity: next slice must retry remaining quantity
  check(e.position(1, "A") == 0, "TWAP no fictional fill");
  liquid(e);
  c.tick = 1; strategy.step(c);
  check(e.position(1, "A") == 0, "TWAP waits for schedule");
  c.tick = 10; strategy.step(c);
  check(e.position(1, "A") == 50, "TWAP reschedules missed volume");
  c.tick = 20; strategy.step(c);
  check(e.position(1, "A") == 100, "TWAP completes nondivisible horizon");
  c.tick = 30; strategy.step(c);
  check(e.position(1, "A") == 100, "TWAP stops at target");
  invariant(e);
}
int main() {
  int failures = 0;
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
    {"matching and accounting", matching}, {"modify and self-trade prevention", modifications},
    {"5000 randomized exchange operations", random_operations}, {"market makers", makers},
    {"momentum, mean reversion and pairs", directional}, {"TWAP", twap}
  };
  for (const auto& [name, test] : tests) {
    try { test(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
