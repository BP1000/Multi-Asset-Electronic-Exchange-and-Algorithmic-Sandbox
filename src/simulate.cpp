#include "sandbox/strategies.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>

using namespace sandbox;
namespace {
constexpr int agent = 1, liquidity_provider = 2, noise_trader = 3, warmup = 60;
const std::vector<std::string> scenarios = {"mean_reverting", "trending", "volatile", "shock"};
struct Frame { Price a, b; int buy_a, sell_a, buy_b, sell_b; bool buy_first; };
std::vector<Frame> generate(std::uint32_t seed, int ticks, const std::string& scenario) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> normal(0, 1);
  std::uniform_int_distribution<int> size(1, 12);
  std::bernoulli_distribution first(0.5);
  std::vector<Frame> tape;
  double common = 10000, spread = 0;
  for (int t = 0; t < ticks + warmup; ++t) {
    const double sigma = scenario == "volatile" ? 8 : 2;
    const double drift = scenario == "trending" ? 0.25 : 0.015 * (10000 - common);
    common = std::max(1000.0, common + drift + sigma * normal(rng));
    spread = 0.92 * spread + 3 * normal(rng);
    const double shift = scenario == "shock" && t >= warmup + ticks / 2 ? 200 : 0;
    tape.push_back({static_cast<Price>(std::llround(std::max(100.0, common + spread + shift))),
                    static_cast<Price>(std::llround(std::max(100.0, common - spread))),
                    size(rng), size(rng), size(rng), size(rng), first(rng)});
  }
  return tape;
}
void provide_liquidity(Exchange& e, const Frame& f) {
  e.cancel_all(liquidity_provider);
  for (const auto& [symbol, fair] : std::map<std::string, Price>{{"A", f.a}, {"B", f.b}})
    for (int depth = 0; depth < 3; ++depth) {
      e.submit(liquidity_provider, symbol, Side::Buy, 50, fair - 3 - depth * 2);
      e.submit(liquidity_provider, symbol, Side::Sell, 50, fair + 3 + depth * 2);
    }
}
void noise(Exchange& e, const Frame& f) {
  for (const auto& symbol : {std::string("A"), std::string("B")}) {
    const int buy = symbol == "A" ? f.buy_a : f.buy_b;
    const int sell = symbol == "A" ? f.sell_a : f.sell_b;
    if (f.buy_first) {
      e.submit(noise_trader, symbol, Side::Buy, buy);
      e.submit(noise_trader, symbol, Side::Sell, sell);
    } else {
      e.submit(noise_trader, symbol, Side::Sell, sell);
      e.submit(noise_trader, symbol, Side::Buy, buy);
    }
  }
}
void run(std::ostream& out, std::ostream* trades, std::ostream* snapshots,
         const std::string& name, const std::string& scenario, std::uint32_t seed, int ticks) {
  Exchange e;
  auto strategy = make_strategy(name);
  const auto tape = generate(seed, ticks, scenario);
  std::map<std::string, std::vector<double>> history;
  double peak = 0, max_drawdown = 0, spread_sum = 0;
  int max_gross_inventory = 0, tracked_a = 0, tracked_b = 0;
  std::size_t scanned = 0;
  const double arrival = tape[warmup].a;
  for (int t = -warmup; t < ticks; ++t) {
    e.set_tick(t);
    const auto& f = tape[static_cast<std::size_t>(t + warmup)];
    provide_liquidity(e, f);
    history["A"].push_back(e.mid("A", static_cast<double>(f.a)));
    history["B"].push_back(e.mid("B", static_cast<double>(f.b)));
    if (t >= 0) {
      Context context{e, agent, t, ticks, history};
      strategy->step(context);
      spread_sum += *e.best("A", Side::Sell) - *e.best("A", Side::Buy);
    }
    noise(e, f);
    for (; scanned < e.trades().size(); ++scanned) {
      const auto& tr = e.trades()[scanned];
      if (tr.buyer == agent || tr.seller == agent) {
        const int delta = tr.buyer == agent ? tr.quantity : -tr.quantity;
        (tr.symbol == "A" ? tracked_a : tracked_b) += delta;
        max_gross_inventory = std::max(max_gross_inventory, std::abs(tracked_a) + std::abs(tracked_b));
      }
      if (trades && t >= 0)
        *trades << name << ',' << scenario << ',' << seed << ',' << tr.tick << ',' << tr.symbol << ','
                << tr.price << ',' << tr.quantity << ',' << tr.buyer << ',' << tr.seller << ',' << tr.maker << ',' << tr.taker << '\n';
    }
    if (t >= 0) {
      const auto pnl = (e.equity(agent, {{"A", f.a}, {"B", f.b}}) - 10'000'000) / 100.0;
      peak = std::max(peak, pnl);
      max_drawdown = std::max(max_drawdown, peak - pnl);
      if (snapshots)
        *snapshots << name << ',' << scenario << ',' << seed << ',' << t << ',' << f.a << ',' << f.b << ','
                   << e.account(agent).cash << ',' << tracked_a << ',' << tracked_b << ',' << pnl << ','
                   << *e.best("A", Side::Buy) << ',' << *e.best("A", Side::Sell) << '\n';
    }
  }
  const auto& final = tape.back();
  const auto& account = e.account(agent);
  const double pnl = (e.equity(agent, {{"A", final.a}, {"B", final.b}}) - 10'000'000) / 100.0;
  double notional = 0;
  int buys = 0, fills = 0;
  for (const auto& tr : e.trades()) if (tr.buyer == agent || tr.seller == agent) {
    ++fills;
    if (tr.buyer == agent && tr.symbol == "A") { notional += tr.price * tr.quantity; buys += tr.quantity; }
  }
  out << name << ',' << scenario << ',' << seed << ',' << ticks << ',' << pnl << ',' << max_drawdown << ','
      << account.fees / 100.0 << ',' << account.filled_shares << ',' << fills << ',' << max_gross_inventory << ','
      << e.position(agent, "A") << ',' << e.position(agent, "B") << ',' << spread_sum / ticks << ',';
  if (name == "twap") {
    out << e.position(agent, "A") / 100.0 << ',';
    if (buys) out << notional / buys - arrival;
  } else out << ',';
  out << '\n';
}
int parse_positive(const std::string& value) {
  std::size_t consumed = 0;
  const int n = std::stoi(value, &consumed);
  if (consumed != value.size() || n <= 0) throw std::invalid_argument("expected positive integer: " + value);
  return n;
}
std::uint32_t parse_seed(const std::string& value) {
  if (value.empty() || value.front() < '0' || value.front() > '9')
    throw std::invalid_argument("seed must be an unsigned 32-bit integer");
  std::size_t consumed = 0;
  const auto seed = std::stoull(value, &consumed);
  if (consumed != value.size() || seed > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("seed must be an unsigned 32-bit integer");
  return static_cast<std::uint32_t>(seed);
}
}
int main(int argc, char** argv) {
  try {
    int runs = 10, ticks = 1000;
    std::uint32_t seed = 42;
    std::string selected_strategy = "all", selected_scenario = "all", output, trace_dir;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (key == "--help") {
        std::cout << "simulate [--strategy all|fixed_spread|inventory_aware|volatility_adaptive|momentum|mean_reversion|pairs|twap] "
                     "[--scenario all|mean_reverting|trending|volatile|shock] [--runs N] [--ticks N] [--seed N] "
                     "[--output results.csv] [--trace-dir directory]\n";
        return 0;
      }
      if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
      const std::string value = argv[++i];
      if (key == "--runs") runs = parse_positive(value);
      else if (key == "--ticks") ticks = parse_positive(value);
      else if (key == "--seed") seed = parse_seed(value);
      else if (key == "--strategy") selected_strategy = value;
      else if (key == "--scenario") selected_scenario = value;
      else if (key == "--output") output = value;
      else if (key == "--trace-dir") trace_dir = value;
      else throw std::invalid_argument("unknown option: " + key);
    }
    if (selected_strategy != "all") (void)make_strategy(selected_strategy);
    if (selected_scenario != "all" && std::find(scenarios.begin(), scenarios.end(), selected_scenario) == scenarios.end())
      throw std::invalid_argument("unknown scenario: " + selected_scenario);
    std::ofstream file, trades, snapshots;
    if (!output.empty()) { file.open(output); if (!file) throw std::runtime_error("cannot open output"); }
    if (!trace_dir.empty()) {
      std::filesystem::create_directories(trace_dir);
      trades.open(std::filesystem::path(trace_dir) / "trades.csv");
      snapshots.open(std::filesystem::path(trace_dir) / "snapshots.csv");
      if (!trades || !snapshots) throw std::runtime_error("cannot open trace files");
      trades << "strategy,scenario,seed,tick,symbol,price_cents,quantity,buyer,seller,maker_order,taker_order\n";
      snapshots << "strategy,scenario,seed,tick,mark_a_cents,mark_b_cents,cash_cents,position_a,position_b,pnl_dollars,bid_a_cents,ask_a_cents\n";
      snapshots << std::fixed << std::setprecision(4);
    }
    auto& out = output.empty() ? std::cout : file;
    out << std::fixed << std::setprecision(6);
    out << "strategy,scenario,seed,ticks,pnl_dollars,max_drawdown_dollars,fees_dollars,filled_shares,fill_events,max_gross_inventory,final_position_a,final_position_b,mean_spread_cents,completion_fraction,arrival_slippage_cents\n";
    for (const auto& scenario : scenarios) if (selected_scenario == "all" || selected_scenario == scenario)
      for (int trial = 0; trial < runs; ++trial)
        for (const auto& name : strategy_names) if (selected_strategy == "all" || selected_strategy == name)
          run(out, trades.is_open() ? &trades : nullptr, snapshots.is_open() ? &snapshots : nullptr,
              name, scenario, seed + static_cast<std::uint32_t>(trial), ticks);
    if (!out.good() || (trades.is_open() && !trades.good()) || (snapshots.is_open() && !snapshots.good()))
      throw std::runtime_error("failed writing results");
  } catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
