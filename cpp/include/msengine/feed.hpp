#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "msengine/order_book.hpp"

namespace msengine {

// Normalized depth message — everything downstream of a feed implementation
// is exchange-agnostic and consumes only this type.
struct DepthUpdate {
    std::int64_t recv_time_ns{};   // local receive timestamp (steady clock)
    std::int64_t exchange_seq{};   // exchange sequence / update id
    bool is_snapshot{};            // true: replace book; false: apply deltas
    std::vector<Level> bids;
    std::vector<Level> asks;
};

enum class FeedState : std::uint8_t { Disconnected, Connecting, Connected };

// Exchange feed interface (strategy pattern). Implementations own their
// network I/O thread; callbacks fire on that thread — the subscriber's job
// is to hand the update off to the compute thread, not to process it.
class IExchangeFeed {
public:
    virtual ~IExchangeFeed() = default;

    using DepthHandler = std::function<void(DepthUpdate&&)>;
    using StateHandler = std::function<void(FeedState)>;

    virtual void on_depth(DepthHandler handler) = 0;
    virtual void on_state(StateHandler handler) = 0;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual FeedState state() const = 0;
};

}  // namespace msengine
