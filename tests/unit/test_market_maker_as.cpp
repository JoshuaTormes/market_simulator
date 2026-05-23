#include <catch_amalgamated.hpp>
#include "agents/MarketMakerAS.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot make_snap(Price mid, Price spread, Tick tick = 0) {
    AgentSnapshot as;
    as.base.tick            = tick;
    as.base.mid_price       = mid;
    as.base.spread          = spread;
    as.base.last_trade_price = mid;
    as.base.realized_vol[0] = 0.01;
    as.base.realized_vol[1] = 0.01;
    as.base.realized_vol[2] = 0.01;
    as.perceived_mid        = mid;
    as.perceived_last       = mid;
    return as;
}

TEST_CASE("MarketMakerAS: emits bid and ask each tick", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 10;

    MarketMakerAS mm(1, "TEST", rng.for_consumer("mm"), p);
    auto actions = mm.on_market_data(make_snap(10000, 2));

    REQUIRE(actions.size() == 2);
    auto* bid = std::get_if<SubmitOrder>(&actions[0]);
    auto* ask = std::get_if<SubmitOrder>(&actions[1]);
    REQUIRE(bid != nullptr);
    REQUIRE(ask != nullptr);
    CHECK(bid->side == Side::Buy);
    CHECK(ask->side == Side::Sell);
}

TEST_CASE("MarketMakerAS: bid < mid < ask", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 5;

    MarketMakerAS mm(1, "TEST", rng.for_consumer("mm"), p);
    Price mid = 10000;
    auto actions = mm.on_market_data(make_snap(mid, 2));

    auto* bid = std::get_if<SubmitOrder>(&actions[0]);
    auto* ask = std::get_if<SubmitOrder>(&actions[1]);
    REQUIRE(bid != nullptr);
    REQUIRE(ask != nullptr);
    CHECK(bid->price < mid);
    CHECK(ask->price > mid);
    CHECK(bid->price < ask->price);
}

TEST_CASE("MarketMakerAS: spread widens as remaining T shrinks", "[mm_as]") {
    // A-S theory: spread increases with remaining_T (risk grows with horizon).
    // We compare two MMs with different T.
    RngService rng(42);
    MarketMakerAS::Params p_long  = {0.1, 1.5, 0.02, 500.0, 5};
    MarketMakerAS::Params p_short = {0.1, 1.5, 0.02,   5.0, 5};

    MarketMakerAS mm_long(1, "T", rng.for_consumer("mm_long"),  p_long);
    MarketMakerAS mm_short(2, "T", rng.for_consumer("mm_short"), p_short);

    auto a_long  = mm_long.on_market_data(make_snap(10000, 2));
    auto a_short = mm_short.on_market_data(make_snap(10000, 2));

    auto bid_l = std::get<SubmitOrder>(a_long[0]).price;
    auto ask_l = std::get<SubmitOrder>(a_long[1]).price;
    auto bid_s = std::get<SubmitOrder>(a_short[0]).price;
    auto ask_s = std::get<SubmitOrder>(a_short[1]).price;

    Price spread_long  = ask_l - bid_l;
    Price spread_short = ask_s - bid_s;
    CHECK(spread_long >= spread_short);
}

TEST_CASE("MarketMakerAS: uses TTL = tick + 2", "[mm_as]") {
    // Quotes submitted at tick N carry TTL = snap.tick + 2 so they survive
    // expire(now) at the tick they are processed and expire the following tick.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 5;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    auto actions = mm.on_market_data(make_snap(10000, 2, /*tick=*/50));

    for (auto& act : actions) {
        auto* so = std::get_if<SubmitOrder>(&act);
        REQUIRE(so != nullptr);
        CHECK(so->ttl_expiry == 52);
    }
}

TEST_CASE("MarketMakerAS: invalid book returns no actions", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    auto actions = mm.on_market_data(make_snap(0, -1));  // invalid spread
    CHECK(actions.empty());
}
