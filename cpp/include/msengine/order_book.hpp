#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace msengine {

// Prices are fixed-point integers (1e-8 units, matching Binance's max precision).
// Floating-point prices invite equality-comparison bugs when matching levels.
using Price = std::int64_t;
using Quantity = double;

inline constexpr std::int64_t PRICE_SCALE = 100'000'000;  // 1.0 == 1e8 ticks

enum class Side : std::uint8_t { Bid, Ask };

struct Level {
    Price price{};
    Quantity qty{};

    bool operator==(const Level&) const = default;
};

// L2 order book for a single symbol.
//
// Levels are stored in contiguous sorted vectors (bids descending, asks
// ascending) rather than a tree map: at depth 20-1000 a cache-friendly
// vector with binary search beats pointer-chasing node containers, and the
// best level is always index 0.
//
// Not thread-safe by design — owned exclusively by the compute thread.
class OrderBook {
public:
    // Replace the entire book (exchange snapshot). Input need not be sorted.
    void apply_snapshot(std::vector<Level> bids, std::vector<Level> asks);

    // Apply one delta: qty == 0 removes the level, otherwise insert/update.
    void apply_update(Side side, Price price, Quantity qty);

    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;

    // Mid-price in ticks; empty if either side of the book is empty.
    std::optional<double> mid_price() const;

    // Spread in ticks; empty if either side is empty. Can be negative on a
    // crossed book (transient state during resync) — callers must handle it.
    std::optional<double> spread() const;

    // (bid_vol - ask_vol) / (bid_vol + ask_vol) over the top `depth` levels.
    // Range [-1, +1]; empty if both sides are empty within `depth`.
    std::optional<double> imbalance(std::size_t depth) const;

    std::span<const Level> bids() const { return bids_; }
    std::span<const Level> asks() const { return asks_; }
    bool empty() const { return bids_.empty() && asks_.empty(); }
    void clear();

private:
    std::vector<Level> bids_;  // sorted by price descending
    std::vector<Level> asks_;  // sorted by price ascending
};

}  // namespace msengine
