#include "msengine/rolling_window.hpp"
#include "msengine/signal_engine.hpp"

#include <gtest/gtest.h>

namespace msengine {
namespace {

constexpr std::int64_t NS = 1;                       // readability
constexpr std::int64_t SEC = 1'000'000'000 * NS;

// ---------- RollingWindow ----------

TEST(RollingWindow, EmptyWindowIsZero) {
    RollingWindow w(5 * SEC);
    EXPECT_TRUE(w.empty());
    EXPECT_DOUBLE_EQ(w.mean(), 0.0);
    EXPECT_DOUBLE_EQ(w.stddev(), 0.0);
    EXPECT_DOUBLE_EQ(w.weighted_mean(), 0.0);
}

TEST(RollingWindow, MeanAndCount) {
    RollingWindow w(5 * SEC);
    w.push(1 * SEC, 10.0);
    w.push(2 * SEC, 20.0);
    w.push(3 * SEC, 30.0);
    EXPECT_EQ(w.count(), 3u);
    EXPECT_DOUBLE_EQ(w.mean(), 20.0);
}

TEST(RollingWindow, EvictsExpiredSamples) {
    RollingWindow w(5 * SEC);
    w.push(0 * SEC, 100.0);
    w.push(1 * SEC, 200.0);
    w.push(7 * SEC, 300.0);  // pushes cutoff to t=2s: first two evicted

    EXPECT_EQ(w.count(), 1u);
    EXPECT_DOUBLE_EQ(w.mean(), 300.0);
    EXPECT_DOUBLE_EQ(w.oldest(), 300.0);
}

TEST(RollingWindow, StddevMatchesClosedForm) {
    RollingWindow w(100 * SEC);
    // Values 2, 4, 4, 4, 5, 5, 7, 9 — textbook population stddev = 2.
    for (double v : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) {
        w.push(1 * SEC, v);
    }
    EXPECT_NEAR(w.stddev(), 2.0, 1e-9);
}

TEST(RollingWindow, WeightedMeanUsesWeights) {
    RollingWindow w(100 * SEC);
    w.push(1 * SEC, 100.0, /*weight=*/1.0);
    w.push(2 * SEC, 200.0, /*weight=*/3.0);
    // (100*1 + 200*3) / 4 = 175
    EXPECT_DOUBLE_EQ(w.weighted_mean(), 175.0);
}

TEST(RollingWindow, EvictionUpdatesAggregatesConsistently) {
    RollingWindow w(2 * SEC);
    w.push(0 * SEC, 1000.0, 5.0);
    w.push(3 * SEC, 10.0, 1.0);  // evicts the first sample entirely

    EXPECT_EQ(w.count(), 1u);
    EXPECT_DOUBLE_EQ(w.mean(), 10.0);
    EXPECT_DOUBLE_EQ(w.weighted_mean(), 10.0);
    EXPECT_DOUBLE_EQ(w.stddev(), 0.0);
}

// ---------- SignalEngine ----------

TEST(SignalEngine, FirstUpdatePopulatesInstantFields) {
    SignalEngine eng(5 * SEC);
    const FeatureRow r =
        eng.on_update(1 * SEC, 42, /*mid=*/104050.0, /*spread=*/100.0,
                      /*imbalance=*/0.25, /*bid_vol=*/6.0, /*ask_vol=*/4.0);

    EXPECT_EQ(r.ts_ns, 1 * SEC);
    EXPECT_EQ(r.exchange_seq, 42);
    EXPECT_DOUBLE_EQ(r.mid, 104050.0);
    EXPECT_DOUBLE_EQ(r.spread, 100.0);
    EXPECT_DOUBLE_EQ(r.imbalance_top5, 0.25);
    EXPECT_DOUBLE_EQ(r.vwap, 104050.0);        // single sample
    EXPECT_DOUBLE_EQ(r.spread_vol, 0.0);       // needs >= 2 samples
    EXPECT_DOUBLE_EQ(r.mean_imbalance, 0.25);
    EXPECT_DOUBLE_EQ(r.momentum, 0.0);         // vs itself
}

TEST(SignalEngine, MomentumMeasuresChangeFromWindowStart) {
    SignalEngine eng(10 * SEC);
    eng.on_update(1 * SEC, 1, 100.0, 1.0, 0.0, 1.0, 1.0);
    eng.on_update(2 * SEC, 2, 101.0, 1.0, 0.0, 1.0, 1.0);
    const FeatureRow r = eng.on_update(3 * SEC, 3, 102.0, 1.0, 0.0, 1.0, 1.0);

    EXPECT_NEAR(r.momentum, 0.02, 1e-12);  // (102-100)/100
}

TEST(SignalEngine, MomentumForgetsExpiredHistory) {
    SignalEngine eng(5 * SEC);
    eng.on_update(0 * SEC, 1, 100.0, 1.0, 0.0, 1.0, 1.0);
    // 100s later: the t=0 sample is long gone; momentum baselines on t=100s.
    eng.on_update(100 * SEC, 2, 200.0, 1.0, 0.0, 1.0, 1.0);
    const FeatureRow r = eng.on_update(101 * SEC, 3, 202.0, 1.0, 0.0, 1.0, 1.0);

    EXPECT_NEAR(r.momentum, 0.01, 1e-12);  // (202-200)/200, not vs 100
}

TEST(SignalEngine, VwapWeightsByDepthVolume) {
    SignalEngine eng(10 * SEC);
    eng.on_update(1 * SEC, 1, 100.0, 1.0, 0.0, /*bid*/ 0.5, /*ask*/ 0.5);  // w=1
    const FeatureRow r =
        eng.on_update(2 * SEC, 2, 200.0, 1.0, 0.0, /*bid*/ 2.0, /*ask*/ 1.0);  // w=3

    EXPECT_DOUBLE_EQ(r.vwap, 175.0);  // (100*1 + 200*3) / 4
}

TEST(SignalEngine, SpreadVolatilityRisesWithVaryingSpread) {
    SignalEngine eng(10 * SEC);
    eng.on_update(1 * SEC, 1, 100.0, 10.0, 0.0, 1.0, 1.0);
    const FeatureRow flat = eng.on_update(2 * SEC, 2, 100.0, 10.0, 0.0, 1.0, 1.0);
    EXPECT_DOUBLE_EQ(flat.spread_vol, 0.0);  // constant spread: zero vol

    const FeatureRow wide = eng.on_update(3 * SEC, 3, 100.0, 40.0, 0.0, 1.0, 1.0);
    EXPECT_GT(wide.spread_vol, 0.0);
}

TEST(SignalEngine, MeanImbalanceSmoothsInstantaneous) {
    SignalEngine eng(10 * SEC);
    eng.on_update(1 * SEC, 1, 100.0, 1.0, +1.0, 1.0, 1.0);
    const FeatureRow r = eng.on_update(2 * SEC, 2, 100.0, 1.0, -1.0, 1.0, 1.0);

    EXPECT_DOUBLE_EQ(r.imbalance_top5, -1.0);   // instant
    EXPECT_DOUBLE_EQ(r.mean_imbalance, 0.0);    // smoothed
}

}  // namespace
}  // namespace msengine
