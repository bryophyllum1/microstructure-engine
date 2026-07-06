// feed_dump: connect to Binance and run the full pipeline —
// feed I/O thread → SPSC queue → compute thread (order book + signals) —
// printing a monitoring line once per second.
//
// Usage: feed_dump [symbol] [seconds]   (defaults: btcusdt, 10)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "msengine/binance_feed.hpp"
#include "msengine/pipeline.hpp"

using namespace msengine;

namespace {
std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
    const std::string symbol = argc > 1 ? argv[1] : "btcusdt";
    const int run_seconds = argc > 2 ? std::atoi(argv[2]) : 10;

    std::signal(SIGINT, on_sigint);

    BinanceFeed feed(symbol);
    feed.on_state([](FeedState s) {
        const char* name = s == FeedState::Connected     ? "CONNECTED"
                           : s == FeedState::Connecting  ? "CONNECTING"
                                                         : "DISCONNECTED";
        std::printf("[feed] %s\n", name);
    });

    MarketDataPipeline pipeline;
    pipeline.start(feed);  // wires depth handler + starts compute thread

    std::printf("Streaming %s for %d seconds (io thread -> spsc -> compute thread)\n",
                symbol.c_str(), run_seconds);
    feed.connect();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(run_seconds);
    const double scale = static_cast<double>(PRICE_SCALE);

    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        const SignalSnapshot& s = pipeline.signals();
        const std::uint64_t updates = s.updates.load(std::memory_order_relaxed);
        if (updates == 0) {
            std::printf("  (no data yet)\n");
            continue;
        }
        std::printf(
            "  mid=%.2f vwap5s=%.2f mom5s=%+.5f%% spread=%.2f sprvol=%.4f "
            "imb=%+.3f updates=%llu dropped=%llu qdepth=%zu\n",
            s.mid_price.load(std::memory_order_relaxed) / scale,
            s.vwap.load(std::memory_order_relaxed) / scale,
            s.momentum.load(std::memory_order_relaxed) * 100.0,
            s.spread.load(std::memory_order_relaxed) / scale,
            s.spread_vol.load(std::memory_order_relaxed) / scale,
            s.imbalance_top5.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(updates),
            static_cast<unsigned long long>(s.dropped.load(std::memory_order_relaxed)),
            pipeline.queue_depth());
    }

    feed.disconnect();
    pipeline.stop();

    const SignalSnapshot& s = pipeline.signals();
    std::printf("Done. %llu updates processed, %llu dropped.\n",
                static_cast<unsigned long long>(s.updates.load()),
                static_cast<unsigned long long>(s.dropped.load()));
    return 0;
}
