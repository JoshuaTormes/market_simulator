#include <catch_amalgamated.hpp>
#include "marketdata/MarketDataPublisher.h"
#include "orderbook/OrderBookV2.h"
#include "orderbook/Trade.h"

static Trade make_trade(Side taker_side, Price price, Qty qty, Tick tick = 1) {
    Trade t{};
    t.taker_side = taker_side;
    t.price      = price;
    t.qty        = qty;
    t.tick       = tick;
    t.seq_no     = 1;
    return t;
}

TEST_CASE("ofi_tick: buy trades produce positive OFI", "[ofi_tick]") {
    OrderBookV2 book;
    MarketDataPublisher pub(book);

    pub.on_trade(make_trade(Side::Buy, 100, 10), 1);
    pub.on_trade(make_trade(Side::Buy, 100, 5),  1);
    auto snap = pub.publish(1);

    CHECK(snap.ofi_tick == Catch::Approx(15.0));
}

TEST_CASE("ofi_tick: sell trades produce negative OFI", "[ofi_tick]") {
    OrderBookV2 book;
    MarketDataPublisher pub(book);

    pub.on_trade(make_trade(Side::Sell, 100, 8), 1);
    auto snap = pub.publish(1);

    CHECK(snap.ofi_tick == Catch::Approx(-8.0));
}

TEST_CASE("ofi_tick: net buy minus sell", "[ofi_tick]") {
    OrderBookV2 book;
    MarketDataPublisher pub(book);

    // Buy 10, Sell 4 → net = +6
    pub.on_trade(make_trade(Side::Buy,  100, 10), 1);
    pub.on_trade(make_trade(Side::Sell, 100, 4),  1);
    auto snap = pub.publish(1);

    CHECK(snap.ofi_tick == Catch::Approx(6.0));
}

TEST_CASE("ofi_tick: resets to zero each tick", "[ofi_tick]") {
    OrderBookV2 book;
    MarketDataPublisher pub(book);

    // Tick 1: buy 10
    pub.on_trade(make_trade(Side::Buy, 100, 10), 1);
    auto s1 = pub.publish(1);
    CHECK(s1.ofi_tick == Catch::Approx(10.0));

    // Tick 2: no trades
    auto s2 = pub.publish(2);
    CHECK(s2.ofi_tick == Catch::Approx(0.0));

    // Tick 3: sell 3
    pub.on_trade(make_trade(Side::Sell, 100, 3), 3);
    auto s3 = pub.publish(3);
    CHECK(s3.ofi_tick == Catch::Approx(-3.0));
}

TEST_CASE("ofi_tick: zero with no trades", "[ofi_tick]") {
    OrderBookV2 book;
    MarketDataPublisher pub(book);

    auto snap = pub.publish(1);
    CHECK(snap.ofi_tick == Catch::Approx(0.0));
}
