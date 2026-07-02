#include "msengine/order_book.hpp"

#include <gtest/gtest.h>

namespace msengine {
namespace {

// Helper: price in whole ticks for readability (104000.5 -> ticks).
constexpr Price px(double p) { return static_cast<Price>(p * PRICE_SCALE); }

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book;

    void load_basic_book() {
        book.apply_snapshot(
            {{px(104000), 1.5}, {px(103800), 0.9}, {px(103500), 3.2}},   // bids
            {{px(104100), 2.1}, {px(104200), 0.8}, {px(104500), 1.2}});  // asks
    }
};

TEST_F(OrderBookTest, EmptyBookReturnsNullopt) {
    EXPECT_TRUE(book.empty());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.mid_price().has_value());
    EXPECT_FALSE(book.spread().has_value());
    EXPECT_FALSE(book.imbalance(5).has_value());
}

TEST_F(OrderBookTest, SnapshotSortsBothLadders) {
    // Deliberately unsorted input.
    book.apply_snapshot(
        {{px(103500), 3.2}, {px(104000), 1.5}, {px(103800), 0.9}},
        {{px(104500), 1.2}, {px(104100), 2.1}, {px(104200), 0.8}});

    ASSERT_EQ(book.bids().size(), 3u);
    EXPECT_EQ(book.bids()[0].price, px(104000));  // best bid first
    EXPECT_EQ(book.bids()[2].price, px(103500));
    ASSERT_EQ(book.asks().size(), 3u);
    EXPECT_EQ(book.asks()[0].price, px(104100));  // best ask first
    EXPECT_EQ(book.asks()[2].price, px(104500));
}

TEST_F(OrderBookTest, BestBidAskMidSpread) {
    load_basic_book();
    EXPECT_EQ(book.best_bid()->price, px(104000));
    EXPECT_EQ(book.best_ask()->price, px(104100));
    EXPECT_DOUBLE_EQ(*book.mid_price(), static_cast<double>(px(104050)));
    EXPECT_DOUBLE_EQ(*book.spread(), static_cast<double>(px(100)));
}

TEST_F(OrderBookTest, UpdateInsertsNewLevelInOrder) {
    load_basic_book();
    book.apply_update(Side::Bid, px(103900), 2.0);

    ASSERT_EQ(book.bids().size(), 4u);
    EXPECT_EQ(book.bids()[1].price, px(103900));  // between 104000 and 103800
    EXPECT_EQ(book.best_bid()->price, px(104000));
}

TEST_F(OrderBookTest, UpdateBetterBidBecomesBest) {
    load_basic_book();
    book.apply_update(Side::Bid, px(104050), 0.5);
    EXPECT_EQ(book.best_bid()->price, px(104050));
}

TEST_F(OrderBookTest, UpdateModifiesExistingLevelQty) {
    load_basic_book();
    book.apply_update(Side::Ask, px(104100), 5.0);

    ASSERT_EQ(book.asks().size(), 3u);  // no new level
    EXPECT_DOUBLE_EQ(book.best_ask()->qty, 5.0);
}

TEST_F(OrderBookTest, ZeroQtyRemovesLevel) {
    load_basic_book();
    book.apply_update(Side::Ask, px(104100), 0.0);

    ASSERT_EQ(book.asks().size(), 2u);
    EXPECT_EQ(book.best_ask()->price, px(104200));  // next level promoted
}

TEST_F(OrderBookTest, ZeroQtyOnMissingLevelIsNoop) {
    load_basic_book();
    book.apply_update(Side::Bid, px(999), 0.0);
    EXPECT_EQ(book.bids().size(), 3u);
}

TEST_F(OrderBookTest, ImbalanceFullyBidHeavy) {
    book.apply_snapshot({{px(100), 10.0}}, {});
    EXPECT_DOUBLE_EQ(*book.imbalance(5), 1.0);
}

TEST_F(OrderBookTest, ImbalanceBalancedBook) {
    book.apply_snapshot({{px(100), 2.0}}, {{px(101), 2.0}});
    EXPECT_DOUBLE_EQ(*book.imbalance(5), 0.0);
}

TEST_F(OrderBookTest, ImbalanceRespectsDepthLimit) {
    // Top level balanced; deep bid liquidity should be ignored at depth 1.
    book.apply_snapshot({{px(100), 1.0}, {px(99), 100.0}},
                        {{px(101), 1.0}});
    EXPECT_DOUBLE_EQ(*book.imbalance(1), 0.0);
    EXPECT_GT(*book.imbalance(2), 0.9);
}

TEST_F(OrderBookTest, CrossedBookReportsNegativeSpread) {
    // Transient crossed state during resync must not crash or clamp.
    book.apply_snapshot({{px(104200), 1.0}}, {{px(104100), 1.0}});
    EXPECT_DOUBLE_EQ(*book.spread(), static_cast<double>(px(-100)));
}

TEST_F(OrderBookTest, ClearEmptiesBook) {
    load_basic_book();
    book.clear();
    EXPECT_TRUE(book.empty());
}

}  // namespace
}  // namespace msengine
