// feed_dump: connect to Binance, drive the order book with live data, and
// print signals once per second. Smoke-test for the feed + book pipeline.
//
// Usage: feed_dump [symbol] [seconds]   (defaults: btcusdt, 10)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "msengine/binance_feed.hpp"
#include "msengine/order_book.hpp"

using namespace msengine;

namespace {
std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
    const std::string symbol = argc > 1 ? argv[1] : "btcusdt";
    const int run_seconds = argc > 2 ? std::atoi(argv[2]) : 10;

    std::signal(SIGINT, on_sigint);

    // Temporary: mutex-guarded book until the SPSC queue lands (Feature 3).
    // The callback runs on the feed's I/O thread; main reads for display.
    OrderBook book;
    std::mutex book_mu;
    std::atomic<std::uint64_t> updates{0};

    BinanceFeed feed(symbol);
    feed.on_state([](FeedState s) {
        const char* name = s == FeedState::Connected     ? "CONNECTED"
                           : s == FeedState::Connecting  ? "CONNECTING"
                                                         : "DISCONNECTED";
        std::printf("[feed] %s\n", name);
    });
    feed.on_depth([&](DepthUpdate&& u) {
        std::lock_guard lock(book_mu);
        book.apply_snapshot(std::move(u.bids), std::move(u.asks));
        updates.fetch_add(1, std::memory_order_relaxed);
    });

    std::printf("Connecting to Binance %s depth stream for %d seconds...\n",
                symbol.c_str(), run_seconds);
    feed.connect();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(run_seconds);
    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::lock_guard lock(book_mu);
        if (book.empty()) {
            std::printf("  (no data yet)\n");
            continue;
        }
        const double scale = static_cast<double>(PRICE_SCALE);
        std::printf(
            "  mid=%.2f spread=%.2f imbalance(top5)=%+.3f updates=%llu\n",
            *book.mid_price() / scale, *book.spread() / scale,
            *book.imbalance(5),
            static_cast<unsigned long long>(updates.load()));
    }

    feed.disconnect();
    std::printf("Done. %llu total updates.\n",
                static_cast<unsigned long long>(updates.load()));
    return 0;
}
