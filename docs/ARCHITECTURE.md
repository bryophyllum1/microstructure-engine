# microstructure-engine — Architecture

## System Overview

Two layers, matching how real quant systems are split:

- **C++ engine** — the latency-sensitive hot path. Consumes exchange WebSocket feeds,
  maintains order books, computes signals, persists data, runs the paper-trading strategy.
- **Python research layer** — offline. Loads persisted data, validates signals
  statistically, runs backtests, produces strategy parameters that feed back into the engine.

```
                    ┌────────────────────────── C++ ENGINE ──────────────────────────┐
                    │                                                                │
 Binance WS ──────► │ FeedHandler ──► [SPSC queue] ──► BookEngine ──► SignalEngine   │
 (Coinbase, ...)    │  (io thread)                     (compute thread)     │        │
                    │                                                       ▼        │
                    │                              [SPSC queue] ──► Persister        │
                    │                                              (writer thread)   │
                    └──────────────────────────────────────────────────│─────────────┘
                                                                       ▼
                                                          Storage (binary tick files
                                                              + Parquet features)
                                                                       │
                    ┌────────────────────── PYTHON RESEARCH ───────────▼─────────────┐
                    │  loaders ──► labeling (forward returns) ──► signal validation  │
                    │  backtest engine (fees/slippage/latency) ──► strategy params   │
                    └─────────────────────────────────────────────────────────────────┘
```

## Threading Model

Three thread roles connected by single-producer/single-consumer lock-free ring buffers:

1. **I/O thread** — owns the WebSocket connection. Parses frames, timestamps them,
   pushes raw messages onto the ingest queue. Never blocks on downstream work.
2. **Compute thread** — pops messages, applies deltas to the order book, recomputes
   signals. This is the hot path: no locks, no heap allocation in steady state.
3. **Writer thread** — batches ticks/features and flushes to disk. Slow I/O can never
   backpressure the compute thread (queue overflow → drop-with-counter, book stays live).

One (io, compute) pair per exchange connection; symbols on a connection share its pair.

## Core Components (C++)

| Component | Responsibility |
|-----------|----------------|
| `IExchangeFeed` | Abstract interface: connect, subscribe, message callbacks. Implementations: `BinanceFeed`, later `CoinbaseFeed` |
| `OrderBook` | L2 book: two sorted price ladders (contiguous arrays, cache-friendly), applies snapshot + delta updates, sequence validation |
| `SignalEngine` | Per-update metrics (mid, spread, imbalance, microprice) + rolling windows (VWAP, spread vol, momentum) using O(1) incremental updates |
| `Recorder` | Appends raw feed messages to disk for deterministic replay |
| `Persister` | Writes tick/feature rows in batches (binary format; converted to Parquet for research) |
| `ReplaySource` | Implements `IExchangeFeed` over a recorded file — same engine code runs live or replayed |
| `RiskManager` | Position limits, drawdown circuit breaker, kill switch (Phase 3) |
| `PaperTrader` | Simulated fills against live book, PnL tracking (Phase 3) |

## Key Design Decisions

1. **SPSC ring buffers, not mutexes** — each queue has exactly one producer and one
   consumer thread, so lock-free single-producer/single-consumer rings give bounded
   latency with no contention.
2. **Replay-first** — `ReplaySource` implements the same interface as a live feed, so
   every component is testable deterministically and research/live use identical code.
3. **Binary on the hot path, Parquet at rest** — writer thread appends fixed-size
   binary records (fast, simple); a conversion step produces Parquet for pandas.
4. **Exchange abstraction at the feed boundary** — normalization to a common internal
   message format happens in each feed implementation; everything downstream is
   exchange-agnostic.
5. **CMake + vcpkg** — dependencies: Boost.Beast or IXWebSocket (WebSocket+TLS),
   simdjson (parsing), spdlog (logging), GoogleTest (tests). Final picks during Phase 1.

## Storage Layout

```
data/
  raw/{exchange}/{symbol}/{date}.feed      # raw recorded messages (replayable)
  ticks/{symbol}/{date}.bin                # per-update book states
  features/{symbol}/{date}.parquet         # rolling features + labels (research input)
```

## Research Layer (Python)

- `research/loaders` — read tick/feature files into pandas/polars
- `research/labeling` — forward returns at multiple horizons (1s, 5s, 30s), careful to avoid look-ahead
- `research/validation` — signal vs forward-return significance tests, information coefficient, regime splits
- `research/backtest` — event-driven backtester with fee/spread/latency/slippage model
