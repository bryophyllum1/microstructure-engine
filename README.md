# microstructure-engine

A production-grade crypto market microstructure system: real-time order book ingestion,
signal computation, statistical validation, and backtesting.

**Not a price-prediction bot.** This system measures structural order book inefficiencies
(bid/ask imbalance, depth asymmetry, trade aggression), validates them statistically, and
only acts on edges that survive realistic transaction-cost modeling — paper trading first,
always.

## Architecture

- **C++ engine** — multithreaded, low-latency hot path: WebSocket feed → order book → signals → persistence
- **Python research layer** — signal validation, labeling, event-driven backtesting

See [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Status

🚧 Phase 1 — data pipeline (C++ feed handler, order book engine)

## Layout

```
cpp/        C++ engine (CMake)
research/   Python research layer
docs/       requirements & architecture
```
