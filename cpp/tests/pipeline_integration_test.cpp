// End-to-end automation test: synthetic exchange feed -> SPSC queue ->
// compute thread (book + signals) -> feature sink -> writer thread -> disk
// -> read back and verify. Real threads throughout; no network.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "msengine/feed.hpp"
#include "msengine/persister.hpp"
#include "msengine/pipeline.hpp"

namespace msengine {
namespace {

namespace fs = std::filesystem;

constexpr Price px(double p) { return static_cast<Price>(p * PRICE_SCALE); }

// Minimal in-process IExchangeFeed: the test drives emissions explicitly.
class FakeFeed final : public IExchangeFeed {
public:
    void on_depth(DepthHandler handler) override { depth_ = std::move(handler); }
    void on_state(StateHandler handler) override { state_ = std::move(handler); }
    void connect() override {
        if (state_) state_(FeedState::Connected);
    }
    void disconnect() override {
        if (state_) state_(FeedState::Disconnected);
    }
    FeedState state() const override { return FeedState::Connected; }

    void emit(DepthUpdate&& u) { depth_(std::move(u)); }

private:
    DepthHandler depth_;
    StateHandler state_;
};

DepthUpdate make_update(std::int64_t seq, Price bid, Price ask, double bid_qty,
                        double ask_qty) {
    DepthUpdate u;
    u.recv_time_ns = seq * 100'000'000;  // 100ms cadence like Binance
    u.exchange_seq = seq;
    u.is_snapshot = true;
    u.bids = {{bid, bid_qty}, {bid - PRICE_SCALE, 1.0}};
    u.asks = {{ask, ask_qty}, {ask + PRICE_SCALE, 1.0}};
    return u;
}

TEST(PipelineIntegration, FeedToDiskEndToEnd) {
    const std::string path =
        (fs::temp_directory_path() / "msengine_integration.msef").string();
    std::error_code ec;
    fs::remove(path, ec);

    constexpr int kUpdates = 500;
    {
        FakeFeed feed;
        MarketDataPipeline pipeline;
        FeatureWriter writer(path);
        pipeline.set_feature_sink(
            [&](const FeatureRow& row) { writer.submit(row); });

        pipeline.start(feed);
        feed.connect();

        // Emit from this thread — it plays the role of the feed's io thread.
        for (int i = 1; i <= kUpdates; ++i) {
            const Price bid = px(100 + i);  // rising market
            feed.emit(make_update(i, bid, bid + 2 * PRICE_SCALE, 5.0, 2.0));
        }

        // Wait until the compute thread has drained everything.
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (pipeline.signals().updates.load() < kUpdates &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_EQ(pipeline.signals().updates.load(), kUpdates);
        EXPECT_EQ(pipeline.signals().dropped.load(), 0u);

        pipeline.stop();
        writer.stop();
        EXPECT_EQ(writer.rows_written(), static_cast<std::uint64_t>(kUpdates));
        EXPECT_EQ(writer.rows_dropped(), 0u);
    }

    // Verify what landed on disk.
    const auto rows = read_feature_file(path);
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(kUpdates));

    for (int i = 1; i <= kUpdates; ++i) {
        const FeatureRow& r = rows[i - 1];
        EXPECT_EQ(r.exchange_seq, i);  // in order, none lost

        // Book math: mid = bid + 1.0, spread = 2.0 (in ticks).
        const double expected_mid = static_cast<double>(px(100 + i) + PRICE_SCALE);
        EXPECT_DOUBLE_EQ(r.mid, expected_mid);
        EXPECT_DOUBLE_EQ(r.spread, 2.0 * PRICE_SCALE);

        // Bid-heavy book (6 vs 3 per side): imbalance = (6-3)/(6+3).
        EXPECT_NEAR(r.imbalance_top5, 1.0 / 3.0, 1e-12);
    }

    // Rising market: momentum must be positive once the window has history.
    EXPECT_GT(rows.back().momentum, 0.0);
    // VWAP lags the last mid in a rising market.
    EXPECT_LT(rows.back().vwap, rows.back().mid);

    fs::remove(path, ec);
}

TEST(PipelineIntegration, SkipsFeatureRowsForOneSidedBook) {
    FakeFeed feed;
    MarketDataPipeline pipeline;
    int sink_calls = 0;
    pipeline.set_feature_sink([&](const FeatureRow&) { ++sink_calls; });

    DepthUpdate u;
    u.recv_time_ns = 1;
    u.exchange_seq = 1;
    u.is_snapshot = true;
    u.bids = {{px(100), 1.0}};  // no asks: mid/spread undefined
    pipeline.process_now(std::move(u));

    EXPECT_EQ(sink_calls, 0);
    EXPECT_EQ(pipeline.signals().updates.load(), 1u);
}

}  // namespace
}  // namespace msengine
