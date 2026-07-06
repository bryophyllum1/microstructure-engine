#pragma once

#include <cmath>
#include <cstdint>
#include <deque>

namespace msengine {

// Time-based rolling window over (timestamp, value, weight) samples.
//
// Maintains running aggregates incrementally: push and evict are O(1)
// amortized, and reading any statistic is O(1) — no rescan of the window
// on the hot path. Samples older than `window_ns` relative to the newest
// push are evicted automatically.
//
// Note on numerics: running sums accumulate floating-point drift over very
// long sessions. Acceptable here (windows are seconds long, values well
// scaled); revisit with Kahan summation if windows grow to hours.
class RollingWindow {
public:
    explicit RollingWindow(std::int64_t window_ns) : window_ns_(window_ns) {}

    void push(std::int64_t ts_ns, double value, double weight = 1.0) {
        samples_.push_back({ts_ns, value, weight});
        sum_ += value;
        sum_sq_ += value * value;
        weight_sum_ += weight;
        weighted_value_sum_ += value * weight;
        evict_before(ts_ns - window_ns_);
    }

    std::size_t count() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }

    double mean() const { return empty() ? 0.0 : sum_ / static_cast<double>(count()); }

    // Population standard deviation over the window.
    double stddev() const {
        if (count() < 2) return 0.0;
        const double n = static_cast<double>(count());
        const double var = (sum_sq_ - sum_ * sum_ / n) / n;
        return var > 0.0 ? std::sqrt(var) : 0.0;  // clamp tiny negatives from fp error
    }

    // Weighted mean (e.g. volume-weighted price). Falls back to plain mean
    // when total weight is zero.
    double weighted_mean() const {
        if (weight_sum_ <= 0.0) return mean();
        return weighted_value_sum_ / weight_sum_;
    }

    // Value of the oldest sample still inside the window.
    double oldest() const { return empty() ? 0.0 : samples_.front().value; }
    double newest() const { return empty() ? 0.0 : samples_.back().value; }

private:
    struct Sample {
        std::int64_t ts_ns;
        double value;
        double weight;
    };

    void evict_before(std::int64_t cutoff_ns) {
        while (!samples_.empty() && samples_.front().ts_ns < cutoff_ns) {
            const Sample& s = samples_.front();
            sum_ -= s.value;
            sum_sq_ -= s.value * s.value;
            weight_sum_ -= s.weight;
            weighted_value_sum_ -= s.value * s.weight;
            samples_.pop_front();
        }
    }

    std::int64_t window_ns_;
    std::deque<Sample> samples_;
    double sum_{0.0};
    double sum_sq_{0.0};
    double weight_sum_{0.0};
    double weighted_value_sum_{0.0};
};

}  // namespace msengine
