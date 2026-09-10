#include <catch_amalgamated.hpp>
#include "agents/InformedTraderKyle.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot make_snap(Price mid, double fundamental_ticks,
                               Price spread = 2, double own_inv = 0.0) {
    AgentSnapshot as;
    as.base.tick             = 1;
    as.base.mid_price        = mid;
    as.base.spread           = spread;
    as.base.last_trade_price = mid;
    as.perceived_mid         = mid;
    as.perceived_last        = mid;
    as.fundamental_value     = fundamental_ticks;
    as.has_fundamental       = true;
    as.own_inventory         = own_inv;
    return as;
}

// Deterministic signal: no observation noise, no heavy tail.
static InformedTraderKyle::Params clean_params() {
    InformedTraderKyle::Params p;
    p.signal_noise_log = 0.0;
    p.pareto_alpha     = 0.0;
    p.lambda_inv       = 0.5;
    p.margin_ticks     = 1.0;
    return p;
}

TEST_CASE("InformedTraderKyle: buys when the fundamental is above the mid", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params());

    auto acts = it.on_market_data(make_snap(10000, 10100.0));
    REQUIRE(acts.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&acts[0]);
    REQUIRE(so != nullptr);
    CHECK(so->side == Side::Buy);
    CHECK(so->type == OrderType::Market);
    CHECK(so->qty  == 50);            // 0.5 lots per tick of a 100-tick gap
}

TEST_CASE("InformedTraderKyle: sells when the fundamental is below the mid", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params());

    auto acts = it.on_market_data(make_snap(10000, 9900.0));
    REQUIRE(acts.size() == 1);
    CHECK(std::get<SubmitOrder>(acts[0]).side == Side::Sell);
}

TEST_CASE("InformedTraderKyle: does not pay a spread wider than the edge", "[kyle]") {
    // A 3-tick gap is real information, but not worth crossing a 20-tick
    // spread.  Trading it anyway is how the old version bled P&L into the
    // market makers.
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params());

    CHECK(it.on_market_data(make_snap(10000, 10003.0, /*spread=*/20)).empty());
    // The same gap is worth taking when the book is tight.
    CHECK(it.on_market_data(make_snap(10000, 10003.0, /*spread=*/2)).size() == 1);
}

TEST_CASE("InformedTraderKyle: unwinds once the gap fits inside the spread", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.unwind_qty = 50;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);

    // Fair price, long 500 lots → sell 50 back.
    auto acts = it.on_market_data(make_snap(10000, 10000.0, 2, /*own_inv=*/500.0));
    REQUIRE(acts.size() == 1);
    auto so = std::get<SubmitOrder>(acts[0]);
    CHECK(so.side == Side::Sell);
    CHECK(so.qty  == 50);

    // Short position unwinds by buying, and never overshoots past flat.
    auto shrt = it.on_market_data(make_snap(10000, 10000.0, 2, /*own_inv=*/-20.0));
    REQUIRE(shrt.size() == 1);
    CHECK(std::get<SubmitOrder>(shrt[0]).side == Side::Buy);
    CHECK(std::get<SubmitOrder>(shrt[0]).qty  == 20);

    // Flat and fairly priced → nothing to do.
    CHECK(it.on_market_data(make_snap(10000, 10000.0, 2, 0.0)).empty());
}

TEST_CASE("InformedTraderKyle: order size is capped", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.max_order_size = 200;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);

    // A 5000-tick gap would ask for 2500 lots.
    auto acts = it.on_market_data(make_snap(10000, 15000.0));
    REQUIRE(acts.size() == 1);
    CHECK(std::get<SubmitOrder>(acts[0]).qty == 200);
}

TEST_CASE("InformedTraderKyle: bigger gaps mean bigger orders", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    InformedTraderKyle small(1, "T", rng.for_consumer("s"), p);
    InformedTraderKyle large(2, "T", rng.for_consumer("l"), p);

    auto a_small = small.on_market_data(make_snap(10000, 10020.0));
    auto a_large = large.on_market_data(make_snap(10000, 10200.0));
    REQUIRE(a_small.size() == 1);
    REQUIRE(a_large.size() == 1);
    CHECK(std::get<SubmitOrder>(a_large[0]).qty
          > std::get<SubmitOrder>(a_small[0]).qty);
}

TEST_CASE("InformedTraderKyle: the signal error is proportional to the price", "[kyle]") {
    // signal_noise_log is a log-noise, so the same sigma gives the same
    // relative signal quality at $1 and at $1000.
    RngService rng(7);
    auto p = clean_params();
    p.signal_noise_log = 0.01;
    p.margin_ticks     = 0.0;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p);

    // With V exactly at the mid, the noise alone decides the side, so both
    // sides must appear over many draws.
    int buys = 0, sells = 0;
    for (int i = 0; i < 200; ++i) {
        auto acts = it.on_market_data(make_snap(10000, 10000.0, 2));
        if (acts.empty()) continue;
        if (std::get<SubmitOrder>(acts[0]).side == Side::Buy) ++buys; else ++sells;
    }
    CHECK(buys  > 20);
    CHECK(sells > 20);
}

TEST_CASE("InformedTraderKyle: no fundamental means no action", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params());
    AgentSnapshot as = make_snap(10000, 10500.0);
    as.has_fundamental = false;
    CHECK(it.on_market_data(as).empty());
}
