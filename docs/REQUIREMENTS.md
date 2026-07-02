# microstructure-engine — Requirements & Scope

## Problem Statement

Order books are noisy but not random. The distribution of resting liquidity (bid/ask
imbalance, depth asymmetry, trade aggression) contains short-lived predictive signal
for near-term price direction. This project builds a production-grade system that:

1. **Measures** structural features of crypto order books in real time
2. **Validates** whether those features have statistically significant predictive power
3. **Quantifies** whether any edge survives realistic transaction costs (fees, spread, slippage, latency)
4. **Executes** (paper first, live only if validated) with discipline and automated risk controls

We are explicitly **not** building a "market prediction" model. We are hunting
structural inefficiencies with a measurable, falsifiable process.

## Goals

- Production-realistic architecture: the kind of system a quant trading firm would run
- Low-latency, multithreaded C++ hot path (feed → book → signal)
- Rigorous Python research layer (statistical validation, backtesting)
- Learn C++ deeply (author's background is Java)
- Portfolio-quality codebase (public repo, tests, CI, docs)

## Non-Goals (for now)

- Live trading with real capital (gated behind validation criteria below)
- Direction prediction at horizons > minutes
- ML models (Phase 2, only after rule-based signals are validated and data is collected)
- Options / equities (crypto spot first; abstractions must not preclude these later)

## Functional Requirements

| ID | Requirement |
|----|-------------|
| F1 | Connect to Binance WebSocket depth stream (btcusdt@depth20@100ms) with auto-reconnect and sequence-gap detection |
| F2 | Maintain an in-memory L2 order book per symbol from snapshots + deltas |
| F3 | Compute per-update signals: mid-price, spread, bid/ask imbalance (top-N), microprice |
| F4 | Compute rolling-window features: VWAP, spread volatility, volume imbalance, momentum |
| F5 | Persist ticks + features to storage for offline research |
| F6 | Exchange connectivity behind an abstract interface (strategy pattern) — adding Coinbase/Kraken must not touch core code |
| F7 | Python research layer: load stored data, label with forward returns, test signal significance |
| F8 | Backtest engine with realistic fill model: fees, spread crossing, latency, slippage |
| F9 | Paper-trading mode: run strategy live against real feed, simulated fills, full PnL tracking |
| F10 | Risk controls: position limits, max drawdown circuit breaker, kill switch |

## Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| N1 | Hot path (message received → signal updated) p99 latency < 100 µs, zero allocation in steady state |
| N2 | Sustain 10k+ book updates/sec per symbol without backpressure |
| N3 | Multithreaded: network I/O, book/signal computation, and persistence on separate threads communicating via lock-free queues |
| N4 | No data loss on disconnect: gaps detected, book resynced from snapshot |
| N5 | Deterministic replay: recorded raw feed can be replayed byte-identically through the engine |
| N6 | Unit tests for book engine and signal math; CI on every push |

## Live-Trading Gate (must ALL pass before real capital)

1. Out-of-sample signal significance (train/test split, no look-ahead)
2. Backtest profitable **after** transaction costs across ≥ 2 market regimes
3. ≥ 4 weeks of paper trading with results consistent with backtest
4. Risk controls tested (kill switch fires under simulated drawdown)

## Phasing

- **Phase 1 — Data pipeline (C++):** feed handler, order book engine, feature computation, persistence
- **Phase 2 — Research (Python):** data loading, labeling, signal validation, backtest engine
- **Phase 3 — Paper trading:** strategy runtime, risk controls, PnL tracking
- **Phase 4 — Evaluate:** live-trading gate review; ML feature-combination experiments
