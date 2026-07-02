#include "msengine/binance_feed.hpp"

#include <chrono>
#include <cstdlib>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <simdjson.h>

namespace msengine {

namespace {

std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Parses one Binance [price, qty] string pair array into a ladder.
bool parse_levels(simdjson::ondemand::array arr, std::vector<Level>& out) {
    for (auto entry : arr) {
        simdjson::ondemand::array pair = entry.get_array();
        auto it = pair.begin();
        if (it == pair.end()) return false;

        std::string_view price_sv;
        if ((*it).get_string().get(price_sv) != simdjson::SUCCESS) return false;
        ++it;
        if (it == pair.end()) return false;
        std::string_view qty_sv;
        if ((*it).get_string().get(qty_sv) != simdjson::SUCCESS) return false;

        Level lvl{};
        if (!parse_price(price_sv, lvl.price)) return false;
        char* end = nullptr;
        lvl.qty = std::strtod(std::string(qty_sv).c_str(), &end);

        // Binance keeps zero-qty placeholder levels in partial-depth
        // snapshots; drop them here so the book never stores empty levels.
        if (lvl.qty > 0.0) out.push_back(lvl);
    }
    return true;
}

}  // namespace

bool parse_price(std::string_view text, Price& out) {
    if (text.empty()) return false;

    std::int64_t int_part = 0;
    std::size_t i = 0;
    for (; i < text.size() && text[i] != '.'; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return false;
        int_part = int_part * 10 + (c - '0');
    }

    std::int64_t frac_part = 0;
    int frac_digits = 0;
    if (i < text.size()) {  // skip '.'
        for (++i; i < text.size(); ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') return false;
            if (frac_digits < 8) {  // beyond 1e-8 precision: truncate
                frac_part = frac_part * 10 + (c - '0');
                ++frac_digits;
            }
        }
    }
    for (; frac_digits < 8; ++frac_digits) frac_part *= 10;

    out = int_part * PRICE_SCALE + frac_part;
    return true;
}

bool parse_binance_depth(std::string_view json, DepthUpdate& out) {
    // simdjson needs padded input; ondemand parser is reused per call site
    // in hot paths, but correctness first — we optimize when profiled.
    thread_local simdjson::ondemand::parser parser;
    simdjson::padded_string padded(json);

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return false;

    out.recv_time_ns = steady_now_ns();
    out.is_snapshot = true;  // partial-depth stream: every message is full top-N
    out.bids.clear();
    out.asks.clear();

    std::int64_t update_id = 0;
    if (doc["lastUpdateId"].get_int64().get(update_id) != simdjson::SUCCESS) return false;
    out.exchange_seq = update_id;

    simdjson::ondemand::array bids;
    if (doc["bids"].get_array().get(bids) != simdjson::SUCCESS) return false;
    if (!parse_levels(bids, out.bids)) return false;

    simdjson::ondemand::array asks;
    if (doc["asks"].get_array().get(asks) != simdjson::SUCCESS) return false;
    if (!parse_levels(asks, out.asks)) return false;

    return true;
}

BinanceFeed::BinanceFeed(std::string symbol) : symbol_(std::move(symbol)) {
    ix::initNetSystem();  // WSAStartup on Windows; no-op elsewhere
    ws_ = std::make_unique<ix::WebSocket>();
    ws_->setUrl("wss://stream.binance.com:9443/ws/" + symbol_ + "@depth20@100ms");
    // IXWebSocket reconnects automatically with capped exponential backoff.
    ws_->setMaxWaitBetweenReconnectionRetries(10'000);  // ms

    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Message:
                handle_message(msg->str);
                break;
            case ix::WebSocketMessageType::Open:
                set_state(FeedState::Connected);
                break;
            case ix::WebSocketMessageType::Close:
            case ix::WebSocketMessageType::Error:
                set_state(FeedState::Disconnected);
                break;
            default:
                break;
        }
    });
}

BinanceFeed::~BinanceFeed() {
    disconnect();
}

void BinanceFeed::connect() {
    set_state(FeedState::Connecting);
    ws_->start();
}

void BinanceFeed::disconnect() {
    ws_->stop();
    set_state(FeedState::Disconnected);
}

void BinanceFeed::handle_message(const std::string& payload) {
    if (!depth_handler_) return;
    DepthUpdate update;
    if (parse_binance_depth(payload, update)) {
        depth_handler_(std::move(update));
    }
}

void BinanceFeed::set_state(FeedState s) {
    state_.store(s, std::memory_order_release);
    if (state_handler_) state_handler_(s);
}

}  // namespace msengine
