# Multi-Asset Electronic Exchange and Algorithmic Sandbox

A C++20 research sandbox for running seven trading strategies against a simulated two-asset limit-order exchange. This branch contains a new correctness-first, in-process implementation. The earlier networking and memory-pool prototype remains on the `exchange` branch; this implementation does not use that unfinished server.

## Build and test

Requires a C++20 compiler and CMake 3.20+. Python 3 enables integration tests and the report generator; no third-party Python packages are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSANDBOX_SANITIZERS=ON
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

The sanitizer option supports Clang and GCC. For faster simulations:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 2
./build-release/simulate --runs 30 --ticks 1000 --seed 42 --output results/benchmark.csv
python3 scripts/summarize.py results/benchmark.csv --output results/benchmark.md
```

Read the checked-in [benchmark report](results/benchmark.md) for the measured results. Tests establish behavior; profitability is evaluated separately and is not a pass/fail condition.

## The seven strategies

All prices and quote distances are integer cents. Position sizes are shares. Each strategy has its own trial, accounts, and books, using the same pre-generated external market inputs for a given seed and scenario. Strategies do not trade against one another in these comparisons.

| Strategy CLI name | Decision rule | Limits / parameters |
|---|---|---|
| `fixed_spread` | Quote the midpoint minus/plus 2 cents; replace quotes each tick. | 5 shares per side; maximum absolute position 40. |
| `inventory_aware` | Shift the quote center down by `0.15 * inventory` cents; use a 2-cent half-spread. A short position shifts it up. | 5 shares per side; maximum absolute position 40. |
| `volatility_adaptive` | Use the inventory shift above; half-spread is `max(2, ceil(2 * volatility))`; size is `max(1, floor(5 / (1 + volatility / 5)))`. | Volatility is the population standard deviation of the last 20 midpoint changes, in cents per tick; maximum absolute position 40. |
| `momentum` | If the 5-observation mean exceeds the 20-observation mean by over 2 cents, target +20 shares; below -2 cents target -20; otherwise flatten. | Requires 20 observations; market orders move toward target. |
| `mean_reversion` | Compute a 20-observation price z-score. Above +1 target -20; below -1 target +20; within +/-0.25 flatten; otherwise hold. | Zero variance flattens; maximum absolute position 20. |
| `pairs` | Compute a rolling 30-observation z-score of `log(A/B)`. Above +1.5 short A / buy B; below -1.5 buy A / short B; within +/-0.25 flatten. | Up to 20 shares per leg; equal-share hedge for similarly priced assets, not an estimated hedge ratio. |
| `twap` | Buy 100 shares over the trial, submitting a market slice every 10 ticks. Divide outstanding shares among the remaining scheduled slots. | Missed or partially filled quantity is retried in later slots. Completion is not guaranteed if liquidity is insufficient. |

Signal strategies use filled inventory when calculating the next order. Market makers cancel their old quotes before placing new ones and reduce quote sizes near their position caps. Pairs legs execute sequentially; a missing second-leg fill can leave an unhedged position. TWAP is an execution algorithm, so its primary measures are completion and arrival-price slippage, rather than profit ranking.

## Exchange behavior

- Separate books for symbols A and B, integer-cent prices, and positive whole-share quantities.
- Best-price matching, FIFO within a price, partial fills, and execution at the resting order's price.
- Limit-order remainders rest; market-order remainders expire.
- Cancellation by unique order ID. Modification cancels and replaces, losing queue priority; invalid replacements preserve the existing order.
- Self-trade prevention cancels the resting order on a self-cross.
- Cash, positions, fill history, and fees: 1 cent per filled share charged to each side.
- Each account starts with $100,000. Shorting is allowed; borrowing costs, margin, and cash-based order rejection are not modeled.

The implementation uses standard containers rather than a memory pool or concurrent queue. Its purpose is to establish inspectable matching and accounting behavior before optimizing throughput.

## Synthetic market model

A trial contains 60 warmup ticks followed by the configured trading horizon. A tick is a simulation step, not a specified duration in seconds. Two latent prices are generated from a common process and a mean-reverting relative spread:

```text
common[t+1] = common[t] + drift + sigma * standard_normal
relative[t+1] = 0.92 * relative[t] + 3 * standard_normal
A = round(common + relative + optional_shock)
B = round(common - relative)
```

The common process starts at 10,000 cents and the relative spread at zero. Common and final asset prices have positive floors. The scenarios are:

| Scenario | Common drift per tick | Common noise sigma | Extra effect |
|---|---|---|---|
| `mean_reverting` | `0.015 * (10000 - common)` | 2 cents | Mean-reverting common price and relative spread. |
| `trending` | +0.25 cents | 2 cents | Upward drift. |
| `volatile` | `0.015 * (10000 - common)` | 8 cents | Larger common shocks. |
| `shock` | `0.015 * (10000 - common)` | 2 cents | A permanently shifts up 200 cents halfway through the trading horizon. |

For each asset, a background liquidity provider refreshes three 50-share levels per side, at 3, 5, and 7 cents from the latent price. Repricing can execute against stale strategy quotes. The strategy then observes book midpoints and acts. A noise trader sends one market buy and one market sell per asset with independent uniformly distributed sizes from 1 to 12 shares; their order is randomized. The next tick repeats this sequence.

Strategies observe midpoint histories, not the latent prices or future tape. Final inventory is marked at the latent external prices for consistent valuation. Strategy fills can change observed midpoints, but cannot change the external price process: there is no persistent endogenous price impact. The volatility-adaptive maker may quote outside the background provider and consequently receive very few fills.

The random tape is generated before each trial with `std::mt19937`. Repeats are deterministic within the same build/toolchain. Standard-library distribution implementations can differ across platforms.

## Experiments and audit traces

```sh
# All seven strategies, four scenarios, 30 independent seeds (42 through 71)
./build-release/simulate --runs 30 --ticks 1000 --seed 42 --output results/benchmark.csv

# One strategy and scenario, including all trades and tick-end account snapshots
./build-release/simulate --strategy pairs --scenario shock --runs 1 --ticks 1000 \
  --output results/pairs-shock.csv --trace-dir /tmp/pairs-shock-trace

./build-release/simulate --help
```

The CSV reports net marked P&L, tick-end maximum drawdown, fees, executed shares, fill-event counts, maximum gross inventory measured after every fill, final positions, and the mean A spread sampled immediately after each strategy action. TWAP additionally reports completion and buy VWAP minus the arrival latent price, in cents per share. Positive slippage is worse for the buyer and excludes fees. Open inventory is not liquidated at the end, so P&L excludes terminal liquidation costs. Fill events count distinct matching events, not fully completed orders or fill rates.

`--trace-dir` writes `trades.csv` and `snapshots.csv`. Trader 1 is the strategy, 2 the liquidity provider, and 3 the noise trader. Trade traces include all trades during the active horizon; snapshots include strategy cash, positions, external marks, P&L, and the tick-end A best bid/ask. Warmup trades are omitted from traces. The files do not record every submitted or canceled order.

The report generator calculates means and approximate 95% normal Monte Carlo intervals across independent seeds. These intervals describe simulation variation under this model, not expected live-market performance. Results are sensitive to replenished liquidity, noise flow, fees, and the external price assumptions. Parameters are fixed defaults, not calibrated or optimized against historical data.

## Validation

- Matching tests cover price priority on both sides, FIFO, partial fills, limit protection, market remainders, cancellation, replacement priority, and self-trade prevention.
- A seeded 5,000-operation randomized test checks share conservation, cash plus fees conservation, order-index consistency, positive live quantities, and uncrossed books after every operation.
- Strategy tests check quote skew, spread widening, long/short caps, signal directions and exits, and TWAP scheduling and retries.
- Integration tests run 56 trials, check identical external paths across strategies and byte-for-byte reproducibility, then rebuild strategy ledgers from trade traces to verify positions, cash, fees, P&L, exposure, drawdown, and TWAP execution metrics. They also validate CLI errors and filtered-trial consistency.

## Source layout

- `include/sandbox/exchange.hpp`: matching and accounting.
- `include/sandbox/strategies.hpp`: the seven strategies and signal helpers.
- `src/simulate.cpp`: market generator, simulation runner, CLI, and CSV traces.
- `tests/`: behavioral, randomized, and integration checks.
- `scripts/summarize.py`: standard-library-only report generation.
- `results/`: reproducible benchmark CSV and its report.
