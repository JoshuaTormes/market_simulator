#include <catch_amalgamated.hpp>
#include "agents/InformedTraderKyle.h"
#include "core/RngService.h"

static AgentSnapshot make_snap_fundamental(Price mid, double fundamental) {
    AgentSnapshot as;
    as.base.mid_price        = mid;
    as.base.spread           = 2;
    as.base.last_trade_price = mid;
    as.perceived_mid         = mid;
    as.fundamental_value     = fundamental;
    as.has_fundamental       = true;
    return as;
}

TEST_CASE("Kyle: positive signal → buy market order", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle::Params p;
    p.lambda_inv     = 0.5;
    p.signal_noise   = 0.0;    // no noise for determinism
    p.min_signal     = 1.0;
    p.max_order_size = 100;

    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);
    auto actions = it.on_market_data(make_snap_fundamental(10000, 10200.0));

    REQUIRE(actions.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&actions[0]);
    REQUIRE(so != nullptr);
    CHECK(so->side == Side::Buy);
    CHECK(so->type == OrderType::Market);
    CHECK(so->qty > 0);
}

TEST_CASE("Kyle: negative signal → sell market order", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle::Params p;
    p.signal_noise   = 0.0;
    p.min_signal     = 1.0;
    p.max_order_size = 100;

    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);
    auto actions = it.on_market_data(make_snap_fundamental(10000, 9800.0));

    REQUIRE(actions.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&actions[0]);
    REQUIRE(so != nullptr);
    CHECK(so->side == Side::Sell);
}

TEST_CASE("Kyle: signal below threshold → no order", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle::Params p;
    p.signal_noise   = 0.0;
    p.min_signal     = 50.0;   // large threshold

    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);
    auto actions = it.on_market_data(make_snap_fundamental(10000, 10010.0));  // signal=10 < 50
    CHECK(actions.empty());
}

TEST_CASE("Kyle: no fundamental → no order", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), {});
    AgentSnapshot as;
    as.has_fundamental = false;
    as.perceived_mid   = 10000;
    CHECK(it.on_market_data(as).empty());
}

TEST_CASE("Kyle: order size proportional to signal", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle::Params p;
    p.lambda_inv     = 1.0;
    p.signal_noise   = 0.0;
    p.min_signal     = 0.0;
    p.max_order_size = 10000;

    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);
    auto small = it.on_market_data(make_snap_fundamental(10000, 10050.0));
    auto large = it.on_market_data(make_snap_fundamental(10000, 10500.0));

    auto* s1 = std::get_if<SubmitOrder>(&small[0]);
    auto* s2 = std::get_if<SubmitOrder>(&large[0]);
    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    CHECK(s2->qty > s1->qty);
}
