#pragma once

#include <cstdint>

#include "msengine/rolling_window.hpp"

namespace msengine {

// One computed feature row per book update. Trivially copyable and fixed
// size on purpose: rows are moved across the SPSC queue to the writer
// thread and written to disk as raw bytes (see persister.hpp).
struct FeatureRow {
    std::int64_t ts_ns{};           // local receive timestamp
    std::int64_t exchange_seq{};    // exchange update id

    // Instantaneous (this update)
    double mid{};                   // ticks
    double spread{};                // ticks
    double imbalance_top5{};        // [-1, +1]
    double bid_vol_top5{};
    double ask_vol_top5{};

    // Rolling window (see SignalEngine window parameter)
    double vwap{};                  // depth-volume-weighted mid, ticks
    double spread_vol{};            // stddev of spread, ticks
    double mean_imbalance{};        // mean of imbalance over window
    double momentum{};              // (mid - oldest_mid) / oldest_mid
};

static_assert(std::is_trivially_copyable_v<FeatureRow>,
              "FeatureRow is persisted as raw bytes");

// Computes rolling-window features incrementally — O(1) per update, no
// rescans. Owned by the compute thread; not thread-safe by design.
class SignalEngine {
public:
    explicit SignalEngine(std::int64_t window_ns)
        : vwap_win_(window_ns),
          spread_win_(window_ns),
          imbalance_win_(window_ns) {}

    FeatureRow on_update(std::int64_t ts_ns, std::int64_t exchange_seq,
                         double mid, double spread, double imbalance,
                         double bid_vol, double ask_vol) {
        vwap_win_.push(ts_ns, mid, bid_vol + ask_vol);
        spread_win_.push(ts_ns, spread);
        imbalance_win_.push(ts_ns, imbalance);

        FeatureRow row;
        row.ts_ns = ts_ns;
        row.exchange_seq = exchange_seq;
        row.mid = mid;
        row.spread = spread;
        row.imbalance_top5 = imbalance;
        row.bid_vol_top5 = bid_vol;
        row.ask_vol_top5 = ask_vol;

        row.vwap = vwap_win_.weighted_mean();
        row.spread_vol = spread_win_.stddev();
        row.mean_imbalance = imbalance_win_.mean();
        const double oldest_mid = vwap_win_.oldest();
        row.momentum = oldest_mid > 0.0 ? (mid - oldest_mid) / oldest_mid : 0.0;

        return row;
    }

private:
    RollingWindow vwap_win_;       // mid weighted by top-5 depth volume
    RollingWindow spread_win_;
    RollingWindow imbalance_win_;
};

}  // namespace msengine
