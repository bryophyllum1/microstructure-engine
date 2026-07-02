#include "msengine/binance_feed.hpp"

#include <gtest/gtest.h>

namespace msengine {
namespace {

TEST(ParsePrice, IntegerAndFraction) {
    Price p{};
    ASSERT_TRUE(parse_price("104123.45000000", p));
    EXPECT_EQ(p, 104123 * PRICE_SCALE + 45'000'000);
}

TEST(ParsePrice, IntegerOnly) {
    Price p{};
    ASSERT_TRUE(parse_price("104000", p));
    EXPECT_EQ(p, 104000 * PRICE_SCALE);
}

TEST(ParsePrice, SmallFraction) {
    Price p{};
    ASSERT_TRUE(parse_price("0.00000001", p));
    EXPECT_EQ(p, 1);
}

TEST(ParsePrice, TruncatesBeyondEightDecimals) {
    Price p{};
    ASSERT_TRUE(parse_price("1.123456789", p));
    EXPECT_EQ(p, 112'345'678);
}

TEST(ParsePrice, RejectsGarbage) {
    Price p{};
    EXPECT_FALSE(parse_price("", p));
    EXPECT_FALSE(parse_price("12a.5", p));
    EXPECT_FALSE(parse_price("1.2.3", p));
}

TEST(ParseBinanceDepth, ValidMessage) {
    const char* json = R"({
        "lastUpdateId": 160,
        "bids": [["104000.50", "1.5"], ["103900.00", "0.9"]],
        "asks": [["104100.00", "2.1"]]
    })";

    DepthUpdate u;
    ASSERT_TRUE(parse_binance_depth(json, u));
    EXPECT_EQ(u.exchange_seq, 160);
    EXPECT_TRUE(u.is_snapshot);
    ASSERT_EQ(u.bids.size(), 2u);
    EXPECT_EQ(u.bids[0].price, 104000 * PRICE_SCALE + 50'000'000);
    EXPECT_DOUBLE_EQ(u.bids[0].qty, 1.5);
    ASSERT_EQ(u.asks.size(), 1u);
    EXPECT_EQ(u.asks[0].price, 104100 * PRICE_SCALE);
    EXPECT_GT(u.recv_time_ns, 0);
}

TEST(ParseBinanceDepth, DropsZeroQtyLevels) {
    const char* json = R"({
        "lastUpdateId": 1,
        "bids": [["100.0", "0.00000000"], ["99.0", "2.0"]],
        "asks": []
    })";

    DepthUpdate u;
    ASSERT_TRUE(parse_binance_depth(json, u));
    ASSERT_EQ(u.bids.size(), 1u);
    EXPECT_EQ(u.bids[0].price, 99 * PRICE_SCALE);
}

TEST(ParseBinanceDepth, RejectsMissingFields) {
    DepthUpdate u;
    EXPECT_FALSE(parse_binance_depth(R"({"bids": [], "asks": []})", u));
    EXPECT_FALSE(parse_binance_depth(R"({"lastUpdateId": 1, "asks": []})", u));
    EXPECT_FALSE(parse_binance_depth("not json", u));
}

TEST(ParseBinanceDepth, FeedsOrderBookEndToEnd) {
    const char* json = R"({
        "lastUpdateId": 42,
        "bids": [["104000.00", "1.5"], ["103800.00", "0.9"]],
        "asks": [["104100.00", "2.1"], ["104200.00", "0.8"]]
    })";

    DepthUpdate u;
    ASSERT_TRUE(parse_binance_depth(json, u));

    OrderBook book;
    book.apply_snapshot(std::move(u.bids), std::move(u.asks));
    EXPECT_DOUBLE_EQ(*book.mid_price(), 104050.0 * PRICE_SCALE);
    EXPECT_NEAR(*book.imbalance(5), (2.4 - 2.9) / (2.4 + 2.9), 1e-12);
}

}  // namespace
}  // namespace msengine
