# Simulation results

Source: `benchmark.csv`. 840 trials; horizons: 1000 ticks.

Profit includes one cent per filled share in fees and marks open inventory at the synthetic external fair price. It excludes liquidation costs. Intervals are approximate 95% normal Monte Carlo intervals for the mean across seeds; they do not measure real-market uncertainty. Max drawdown is sampled at tick ends.

| Scenario | Strategy | Runs | Mean P&L ($) | Approx. 95% interval ($) | Mean max drawdown ($) | Mean filled shares | Mean peak gross inventory |
|---|---|---:|---:|---:|---:|---:|---:|
| mean_reverting | fixed_spread | 30 | 60.41 | 57.54 to 63.28 | 11.01 | 7185.5 | 39.1 |
| mean_reverting | inventory_aware | 30 | 4.19 | 2.69 to 5.68 | 3.03 | 4810.9 | 12.7 |
| mean_reverting | mean_reversion | 30 | -86.21 | -89.89 to -82.54 | 87.43 | 3448.7 | 20.0 |
| mean_reverting | momentum | 30 | -162.95 | -172.18 to -153.71 | 164.79 | 2878.7 | 20.0 |
| mean_reverting | pairs | 30 | -58.12 | -64.08 to -52.16 | 62.79 | 4177.3 | 40.0 |
| mean_reverting | volatility_adaptive | 30 | 0.04 | -0.40 to 0.47 | 2.67 | 33.7 | 6.3 |
| shock | fixed_spread | 30 | 54.00 | 39.21 to 68.79 | 28.71 | 7189.3 | 39.3 |
| shock | inventory_aware | 30 | -1.57 | -4.32 to 1.18 | 8.39 | 4812.9 | 12.7 |
| shock | mean_reversion | 30 | -91.85 | -103.76 to -79.94 | 93.43 | 3400.7 | 20.0 |
| shock | momentum | 30 | -152.20 | -164.97 to -139.43 | 154.05 | 2849.3 | 20.0 |
| shock | pairs | 30 | -61.02 | -72.56 to -49.48 | 68.93 | 4132.0 | 40.0 |
| shock | volatility_adaptive | 30 | -2.61 | -4.55 to -0.67 | 5.76 | 36.1 | 6.1 |
| trending | fixed_spread | 30 | 15.82 | 2.84 to 28.79 | 23.85 | 7161.6 | 40.0 |
| trending | inventory_aware | 30 | 3.03 | 1.67 to 4.40 | 3.83 | 4781.6 | 12.7 |
| trending | mean_reversion | 30 | -96.90 | -101.56 to -92.24 | 97.85 | 3150.7 | 20.0 |
| trending | momentum | 30 | -134.38 | -144.75 to -124.01 | 137.30 | 2684.0 | 20.0 |
| trending | pairs | 30 | -57.26 | -62.88 to -51.64 | 61.81 | 4214.7 | 40.0 |
| trending | volatility_adaptive | 30 | -3.98 | -5.94 to -2.02 | 6.56 | 35.2 | 6.8 |
| volatile | fixed_spread | 30 | 44.23 | 37.47 to 50.99 | 38.67 | 8307.8 | 39.9 |
| volatile | inventory_aware | 30 | -96.25 | -98.49 to -94.01 | 96.75 | 5882.1 | 13.0 |
| volatile | mean_reversion | 30 | -74.07 | -87.54 to -60.61 | 89.19 | 3128.7 | 20.0 |
| volatile | momentum | 30 | -154.71 | -173.64 to -135.77 | 172.34 | 2715.3 | 20.0 |
| volatile | pairs | 30 | -58.39 | -64.66 to -52.11 | 62.97 | 4194.7 | 40.0 |
| volatile | volatility_adaptive | 30 | 1.06 | -0.52 to 2.63 | 9.39 | 45.7 | 7.7 |

## TWAP execution

TWAP buys 100 shares. Positive arrival slippage means the fill VWAP was above the arrival fair price (worse for the buyer); this measure excludes fees. A market drift can dominate this number, so it does not isolate market impact. TWAP P&L is not ranked against signal strategies.

| Scenario | Runs | Mean completion | Mean arrival slippage (cents/share) | Mean fees ($) |
|---|---:|---:|---:|---:|
| mean_reverting | 30 | 100.0% | 2.79 | 1.00 |
| shock | 30 | 100.0% | 102.79 | 1.00 |
| trending | 30 | 100.0% | 129.97 | 1.00 |
| volatile | 30 | 100.0% | 1.61 | 1.00 |

## Interpretation

These are controlled synthetic experiments, not calibrated backtests. Background liquidity is replenished every tick, noise orders are bounded and balanced in direction, and prices follow the configured external process. A passive strategy can benefit mechanically from this setup. Low losses accompanied by few fills may simply mean low participation. Compare results by scenario, activity, fees and exposure rather than treating a profit ranking as evidence of a durable trading edge.
