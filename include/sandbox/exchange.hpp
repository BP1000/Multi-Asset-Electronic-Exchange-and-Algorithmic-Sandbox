#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sandbox {
using Price = std::int64_t; // integer cents
using OrderId = std::uint64_t;
enum class Side { Buy, Sell };
struct Order {
  OrderId id;
  int trader;
  std::string symbol;
  Side side;
  Price price;
  int quantity;
};
struct Trade {
  int tick;
  std::string symbol;
  Price price;
  int quantity;
  int buyer, seller;
  OrderId maker, taker;
};
struct Account {
  std::int64_t cash = 10'000'000;
  std::map<std::string, int> positions;
  std::int64_t fees = 0;
  int filled_shares = 0;
};
struct Submission { OrderId id; int filled; int resting; };
struct Book {
  std::map<Price, std::deque<OrderId>> bids, asks;
};
class Exchange {
  std::map<std::string, Book> books_;
  std::unordered_map<OrderId, Order> orders_;
  std::map<int, Account> accounts_;
  std::vector<Trade> trades_;
  OrderId next_ = 1;
  int tick_ = 0;
  Price fee_;
  void settle(int trader, const std::string& symbol, int signed_qty, Price price) {
    auto& a = accounts_[trader];
    const auto fee = static_cast<std::int64_t>(std::abs(signed_qty)) * fee_;
    a.cash -= price * signed_qty + fee;
    a.positions[symbol] += signed_qty;
    a.fees += fee;
    a.filled_shares += std::abs(signed_qty);
  }
public:
  explicit Exchange(Price fee_per_share = 1) : fee_(fee_per_share) {
    if (fee_ < 0) throw std::invalid_argument("negative fee");
  }
  void set_tick(int tick) { tick_ = tick; }
  const Account& account(int trader) const {
    static const Account empty;
    const auto it = accounts_.find(trader);
    return it == accounts_.end() ? empty : it->second;
  }
  int position(int trader, const std::string& symbol) const {
    const auto& p = account(trader).positions;
    const auto it = p.find(symbol);
    return it == p.end() ? 0 : it->second;
  }
  const auto& trades() const { return trades_; }
  const auto& orders() const { return orders_; }
  const auto& accounts() const { return accounts_; }
  const auto& books() const { return books_; }
  std::optional<Price> best(const std::string& symbol, Side side) const {
    const auto it = books_.find(symbol);
    if (it == books_.end()) return {};
    const auto& levels = side == Side::Buy ? it->second.bids : it->second.asks;
    if (levels.empty()) return {};
    return side == Side::Buy ? levels.rbegin()->first : levels.begin()->first;
  }
  double mid(const std::string& symbol, double fallback) const {
    const auto b = best(symbol, Side::Buy), a = best(symbol, Side::Sell);
    return b && a ? (*b + *a) / 2.0 : fallback;
  }
  bool cancel(OrderId id) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) return false;
    const auto order = it->second;
    auto& book = books_.at(order.symbol);
    auto& levels = order.side == Side::Buy ? book.bids : book.asks;
    auto level = levels.find(order.price);
    std::erase(level->second, id);
    if (level->second.empty()) levels.erase(level);
    orders_.erase(it);
    return true;
  }
  void cancel_all(int trader) {
    std::vector<OrderId> ids;
    for (const auto& [id, order] : orders_) if (order.trader == trader) ids.push_back(id);
    for (const auto id : ids) cancel(id);
  }
  Submission submit(int trader, const std::string& symbol, Side side,
                    int quantity, std::optional<Price> limit = {}) {
    if (symbol.empty() || quantity <= 0 || (limit && *limit <= 0))
      throw std::invalid_argument("positive quantity/price and nonempty symbol required");
    const auto id = next_++;
    int remaining = quantity;
    auto& book = books_[symbol];
    auto& opposite = side == Side::Buy ? book.asks : book.bids;
    while (remaining > 0 && !opposite.empty()) {
      auto level = side == Side::Buy ? opposite.begin() : std::prev(opposite.end());
      const auto price = level->first;
      if (limit && (side == Side::Buy ? price > *limit : price < *limit)) break;
      const auto maker_id = level->second.front();
      auto& maker = orders_.at(maker_id);
      // Cancel the resting order on a self-cross, then continue matching.
      if (maker.trader == trader) { cancel(maker_id); continue; }
      const int qty = std::min(remaining, maker.quantity);
      const int buyer = side == Side::Buy ? trader : maker.trader;
      const int seller = side == Side::Sell ? trader : maker.trader;
      settle(buyer, symbol, qty, price);
      settle(seller, symbol, -qty, price);
      trades_.push_back({tick_, symbol, price, qty, buyer, seller, maker_id, id});
      remaining -= qty;
      maker.quantity -= qty;
      if (maker.quantity == 0) {
        orders_.erase(maker_id);
        level->second.pop_front();
        if (level->second.empty()) opposite.erase(level);
      }
    }
    const int filled = quantity - remaining;
    if (remaining > 0 && limit) {
      orders_.emplace(id, Order{id, trader, symbol, side, *limit, remaining});
      (side == Side::Buy ? book.bids : book.asks)[*limit].push_back(id);
      return {id, filled, remaining};
    }
    return {id, filled, 0}; // unfilled market quantity expires
  }
  Submission modify(OrderId id, Price price, int quantity) {
    if (price <= 0 || quantity <= 0) throw std::invalid_argument("invalid replacement");
    const auto order = orders_.at(id);
    cancel(id); // every replacement loses time priority
    return submit(order.trader, order.symbol, order.side, quantity, price);
  }
  std::int64_t equity(int trader, const std::map<std::string, Price>& marks) const {
    const auto& a = account(trader);
    auto value = a.cash;
    for (const auto& [symbol, qty] : a.positions) value += marks.at(symbol) * qty;
    return value;
  }
};
} // namespace sandbox
