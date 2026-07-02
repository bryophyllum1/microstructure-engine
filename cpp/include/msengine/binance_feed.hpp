#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "msengine/feed.hpp"

namespace ix {
class WebSocket;
}

namespace msengine {

// Parses one message from the Binance partial-depth stream
// (<symbol>@depth20@100ms). Each message carries the full top-20 book, so
// it is treated as a snapshot. Exposed standalone so tests need no network.
//
// Returns false on malformed input; `out` is unspecified in that case.
bool parse_binance_depth(std::string_view json, DepthUpdate& out);

// Parses a decimal string like "104123.45000000" into fixed-point Price
// (1e-8 ticks) without going through double (which would lose precision
// for large prices). Returns false on malformed input.
bool parse_price(std::string_view text, Price& out);

class BinanceFeed final : public IExchangeFeed {
public:
    // symbol: lowercase Binance symbol, e.g. "btcusdt"
    explicit BinanceFeed(std::string symbol);
    ~BinanceFeed() override;

    void on_depth(DepthHandler handler) override { depth_handler_ = std::move(handler); }
    void on_state(StateHandler handler) override { state_handler_ = std::move(handler); }

    void connect() override;
    void disconnect() override;
    FeedState state() const override { return state_.load(std::memory_order_acquire); }

private:
    void handle_message(const std::string& payload);
    void set_state(FeedState s);

    std::string symbol_;
    std::unique_ptr<ix::WebSocket> ws_;
    DepthHandler depth_handler_;
    StateHandler state_handler_;
    std::atomic<FeedState> state_{FeedState::Disconnected};
};

}  // namespace msengine
