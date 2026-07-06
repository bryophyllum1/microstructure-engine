#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "msengine/feed.hpp"
#include "msengine/order_book.hpp"
#include "msengine/spsc_queue.hpp"

namespace msengine {

// Monitoring snapshot of the latest computed signals. Read from any thread;
// each field is individually atomic (torn *sets* are acceptable for display,
// individual values are never torn).
struct SignalSnapshot {
    std::atomic<double> mid_price{0.0};        // in ticks
    std::atomic<double> spread{0.0};           // in ticks
    std::atomic<double> imbalance_top5{0.0};
    std::atomic<std::uint64_t> updates{0};
    std::atomic<std::uint64_t> dropped{0};
};

// Owns the io→compute handoff for one symbol:
//
//   feed I/O thread ──try_push──► SpscQueue ──try_pop──► compute thread
//                                                        (owns OrderBook)
//
// The order book is touched by exactly one thread, so it needs no locks.
// If the compute thread ever falls behind, try_push fails and the update
// is DROPPED (counted): for partial-depth snapshots the next message
// supersedes the lost one, so dropping is strictly better than letting
// the exchange-facing I/O thread block.
class MarketDataPipeline {
public:
    explicit MarketDataPipeline(std::size_t queue_capacity = 4096)
        : queue_(queue_capacity) {}

    ~MarketDataPipeline() { stop(); }

    // Called by the feed's I/O thread (producer side).
    void submit(DepthUpdate&& update) {
        if (!queue_.try_push(std::move(update))) {
            signals_.dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Attach as the feed's depth handler and start the compute thread.
    void start(IExchangeFeed& feed) {
        feed.on_depth([this](DepthUpdate&& u) { submit(std::move(u)); });
        running_.store(true, std::memory_order_release);
        compute_ = std::thread([this] { run(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (compute_.joinable()) compute_.join();
    }

    const SignalSnapshot& signals() const { return signals_; }
    std::size_t queue_depth() const { return queue_.size_approx(); }

private:
    void run() {
        DepthUpdate update;
        int idle_spins = 0;
        while (running_.load(std::memory_order_acquire)) {
            if (!queue_.try_pop(update)) {
                // Spin briefly for latency, then back off to spare the core.
                if (++idle_spins < 1000) {
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
                continue;
            }
            idle_spins = 0;
            apply(update);
        }
    }

    void apply(DepthUpdate& u) {
        if (u.is_snapshot) {
            book_.apply_snapshot(std::move(u.bids), std::move(u.asks));
        } else {
            for (const Level& l : u.bids) book_.apply_update(Side::Bid, l.price, l.qty);
            for (const Level& l : u.asks) book_.apply_update(Side::Ask, l.price, l.qty);
        }

        if (auto mid = book_.mid_price())
            signals_.mid_price.store(*mid, std::memory_order_relaxed);
        if (auto s = book_.spread())
            signals_.spread.store(*s, std::memory_order_relaxed);
        if (auto imb = book_.imbalance(5))
            signals_.imbalance_top5.store(*imb, std::memory_order_relaxed);
        signals_.updates.fetch_add(1, std::memory_order_relaxed);
    }

    SpscQueue<DepthUpdate> queue_;
    OrderBook book_;  // compute-thread-owned; never touched elsewhere
    SignalSnapshot signals_;
    std::atomic<bool> running_{false};
    std::thread compute_;
};

}  // namespace msengine
