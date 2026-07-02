#include "msengine/order_book.hpp"

#include <algorithm>

namespace msengine {

namespace {

// Comparators defining each ladder's sort order.
constexpr auto bid_less = [](const Level& a, const Level& b) { return a.price > b.price; };
constexpr auto ask_less = [](const Level& a, const Level& b) { return a.price < b.price; };

template <typename Cmp>
void upsert(std::vector<Level>& ladder, Price price, Quantity qty, Cmp cmp) {
    const Level key{price, 0.0};
    auto it = std::lower_bound(ladder.begin(), ladder.end(), key, cmp);
    const bool found = it != ladder.end() && it->price == price;

    if (qty == 0.0) {
        if (found) ladder.erase(it);
        return;
    }
    if (found) {
        it->qty = qty;
    } else {
        ladder.insert(it, Level{price, qty});
    }
}

}  // namespace

void OrderBook::apply_snapshot(std::vector<Level> bids, std::vector<Level> asks) {
    bids_ = std::move(bids);
    asks_ = std::move(asks);
    std::sort(bids_.begin(), bids_.end(), bid_less);
    std::sort(asks_.begin(), asks_.end(), ask_less);
}

void OrderBook::apply_update(Side side, Price price, Quantity qty) {
    if (side == Side::Bid) {
        upsert(bids_, price, qty, bid_less);
    } else {
        upsert(asks_, price, qty, ask_less);
    }
}

std::optional<Level> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.front();
}

std::optional<Level> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.front();
}

std::optional<double> OrderBook::mid_price() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return (static_cast<double>(bids_.front().price) +
            static_cast<double>(asks_.front().price)) / 2.0;
}

std::optional<double> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return static_cast<double>(asks_.front().price - bids_.front().price);
}

std::optional<double> OrderBook::imbalance(std::size_t depth) const {
    double bid_vol = 0.0;
    for (std::size_t i = 0; i < depth && i < bids_.size(); ++i) bid_vol += bids_[i].qty;
    double ask_vol = 0.0;
    for (std::size_t i = 0; i < depth && i < asks_.size(); ++i) ask_vol += asks_[i].qty;

    const double total = bid_vol + ask_vol;
    if (total == 0.0) return std::nullopt;
    return (bid_vol - ask_vol) / total;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
}

}  // namespace msengine
