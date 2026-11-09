#pragma once
#include "exchange.hpp"
#include <cmath>
#include <memory>
#include <numeric>

namespace sandbox {
struct Context {
  Exchange& exchange;
  int trader;
  int tick;
  int horizon;
  const std::map<std::string, std::vector<double>>& history;
  double mid(const std::string& symbol) const { return history.at(symbol).back(); }
};
inline double mean_tail(const std::vector<double>& values, std::size_t window) {
  const auto n = std::min(window, values.size());
  return std::accumulate(values.end() - static_cast<std::ptrdiff_t>(n), values.end(), 0.0) / n;
}
inline double std_tail(const std::vector<double>& values, std::size_t window) {
  const auto n = std::min(window, values.size());
  const auto mean = mean_tail(values, n);
  double sum = 0;
  for (auto it = values.end() - static_cast<std::ptrdiff_t>(n); it != values.end(); ++it)
    sum += (*it - mean) * (*it - mean);
  return std::sqrt(sum / n);
}
inline double change_volatility(const std::vector<double>& values, std::size_t window = 20) {
  if (values.size() < 2) return 0;
  std::vector<double> differences;
  const auto start = values.size() > window ? values.size() - window : 1;
  for (auto i = start; i < values.size(); ++i) differences.push_back(values[i] - values[i - 1]);
  return std_tail(differences, differences.size());
}
inline void target_position(Context& c, const std::string& symbol, int target) {
  const int delta = target - c.exchange.position(c.trader, symbol);
  if (delta != 0) c.exchange.submit(c.trader, symbol, delta > 0 ? Side::Buy : Side::Sell, std::abs(delta));
}
class Strategy {
public:
  virtual ~Strategy() = default;
  virtual std::string name() const = 0;
  virtual void step(Context& context) = 0;
};
enum class MakerMode { Fixed, Inventory, Volatility };
class MarketMaker final : public Strategy {
  MakerMode mode_;
public:
  static constexpr int inventory_limit = 40;
  explicit MarketMaker(MakerMode mode) : mode_(mode) {}
  std::string name() const override {
    if (mode_ == MakerMode::Fixed) return "fixed_spread";
    return mode_ == MakerMode::Inventory ? "inventory_aware" : "volatility_adaptive";
  }
  void step(Context& c) override {
    c.exchange.cancel_all(c.trader);
    const auto& history = c.history.at("A");
    const auto inventory = c.exchange.position(c.trader, "A");
    const auto vol = change_volatility(history);
    const int half_spread = mode_ == MakerMode::Volatility ? std::max(2, static_cast<int>(std::ceil(2 * vol))) : 2;
    const double skew = mode_ == MakerMode::Fixed ? 0 : inventory * 0.15;
    const double center = c.mid("A") - skew;
    const int size = mode_ == MakerMode::Volatility ? std::max(1, static_cast<int>(5 / (1 + vol / 5))) : 5;
    const auto bid = std::max<Price>(1, static_cast<Price>(std::floor(center - half_spread)));
    const auto ask = std::max<Price>(bid + 1, static_cast<Price>(std::ceil(center + half_spread)));
    const int buy_size = std::min(size, inventory_limit - inventory);
    const int sell_size = std::min(size, inventory_limit + inventory);
    if (buy_size > 0) c.exchange.submit(c.trader, "A", Side::Buy, buy_size, bid);
    if (sell_size > 0) c.exchange.submit(c.trader, "A", Side::Sell, sell_size, ask);
  }
};
class Momentum final : public Strategy {
public:
  std::string name() const override { return "momentum"; }
  void step(Context& c) override {
    const auto& h = c.history.at("A");
    if (h.size() < 20) return;
    const auto signal = mean_tail(h, 5) - mean_tail(h, 20);
    target_position(c, "A", signal > 2 ? 20 : signal < -2 ? -20 : 0);
  }
};
class MeanReversion final : public Strategy {
public:
  std::string name() const override { return "mean_reversion"; }
  void step(Context& c) override {
    const auto& h = c.history.at("A");
    if (h.size() < 20) return;
    const auto stddev = std_tail(h, 20);
    if (stddev < 0.01) { target_position(c, "A", 0); return; }
    const auto z = (h.back() - mean_tail(h, 20)) / stddev;
    if (z > 1) target_position(c, "A", -20);
    else if (z < -1) target_position(c, "A", 20);
    else if (std::abs(z) < 0.25) target_position(c, "A", 0);
  }
};
class PairsTrading final : public Strategy {
public:
  std::string name() const override { return "pairs"; }
  void step(Context& c) override {
    const auto& a = c.history.at("A");
    const auto& b = c.history.at("B");
    if (a.size() < 30 || b.size() != a.size()) return;
    std::vector<double> ratios;
    for (auto i = a.size() - 30; i < a.size(); ++i) ratios.push_back(std::log(a[i] / b[i]));
    const auto deviation = std_tail(ratios, 30);
    if (deviation < 1e-9) {
      target_position(c, "A", 0); target_position(c, "B", 0); return;
    }
    const auto z = (ratios.back() - mean_tail(ratios, 30)) / deviation;
    int target_a = c.exchange.position(c.trader, "A");
    if (z > 1.5) target_a = -20;
    else if (z < -1.5) target_a = 20;
    else if (std::abs(z) < 0.25) target_a = 0;
    target_position(c, "A", target_a);
    // Equal-share hedge for similarly priced assets. Legs execute sequentially.
    target_position(c, "B", -c.exchange.position(c.trader, "A"));
  }
};
class Twap final : public Strategy {
  int target_;
  int interval_;
public:
  explicit Twap(int target = 100, int interval = 10) : target_(target), interval_(interval) {
    if (target <= 0 || interval <= 0) throw std::invalid_argument("invalid TWAP parameters");
  }
  std::string name() const override { return "twap"; }
  void step(Context& c) override {
    if (c.tick < 0 || c.tick >= c.horizon || c.tick % interval_ != 0) return;
    const int remaining = target_ - c.exchange.position(c.trader, "A");
    if (remaining <= 0) return;
    const int slots = 1 + (c.horizon - 1 - c.tick) / interval_;
    c.exchange.submit(c.trader, "A", Side::Buy, (remaining + slots - 1) / slots);
  }
};
inline const std::vector<std::string> strategy_names = {
  "fixed_spread", "inventory_aware", "volatility_adaptive", "momentum", "mean_reversion", "pairs", "twap"
};
inline std::unique_ptr<Strategy> make_strategy(const std::string& name) {
  if (name == "fixed_spread") return std::make_unique<MarketMaker>(MakerMode::Fixed);
  if (name == "inventory_aware") return std::make_unique<MarketMaker>(MakerMode::Inventory);
  if (name == "volatility_adaptive") return std::make_unique<MarketMaker>(MakerMode::Volatility);
  if (name == "momentum") return std::make_unique<Momentum>();
  if (name == "mean_reversion") return std::make_unique<MeanReversion>();
  if (name == "pairs") return std::make_unique<PairsTrading>();
  if (name == "twap") return std::make_unique<Twap>();
  throw std::invalid_argument("unknown strategy: " + name);
}
} // namespace sandbox
