#include <catch_amalgamated.hpp>
#include "agents/MarketMakerAS.h"
#include "marketdata/AgentSnapshot.h"
#include "core/EventBus.h"
#include "core/RngService.h"
#include "economics/NewsEvent.h"
#include <cmath>

// Glosten-Milgrom belief dynamics: order flow, executed prices and public
// announcements move the maker's private mid.

static AgentSnapshot make_snap(Price mid, Tick tick = 1,
                               double ofi = 0.0, double own_inv = 0.0,
                               Price last = 0) {
    AgentSnapshot as;
    as.base.tick             = tick;
    as.base.mid_price        = mid;
    as.base.spread           = 2;
    as.base.last_trade_price = last > 0 ? last : mid;
    as.base.realized_vol[0]  = 0.0;
    as.base.ofi_tick         = ofi;
    as.perceived_mid         = mid;
    as.perceived_last        = last > 0 ? last : mid;
    as.own_inventory         = own_inv;
    return as;
}

static const SubmitOrder& quote(const std::vector<Action>& acts, Side side) {
    for (const auto& a : acts)
        if (const auto* so = std::get_if<SubmitOrder>(&a))
            if (so->side == side) return *so;
    throw std::runtime_error("no quote on that side");
}

TEST_CASE("MarketMakerAS: long inventory skews both quotes down", "[mm_bayesian]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.inventory_skew_ticks = 4.0;
    p.q_soft               = 300.0;

    MarketMakerAS flat(1, "T", rng.for_consumer("flat"), p);
    MarketMakerAS lng (2, "T", rng.for_consumer("long"), p);

    auto a0 = flat.on_market_data(make_snap(10000, 1, 0.0,   0.0));
    auto a1 = lng .on_market_data(make_snap(10000, 1, 0.0, 300.0));

    // At |q| = q_soft the reservation moves a full inventory_skew_ticks down.
    CHECK(quote(a1, Side::Sell).price < quote(a0, Side::Sell).price);
    CHECK(quote(a0, Side::Sell).price - quote(a1, Side::Sell).price
          == Catch::Approx(4.0).margin(1.0));
}

TEST_CASE("MarketMakerAS: signed flow moves the belief", "[mm_bayesian]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.lambda_kyle               = 0.02;
    p.belief_decay              = 0.0;   // no pull back to the tape
    p.adverse_sel_ticks_per_lot = 0.0;   // isolate the belief effect

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    mm.on_market_data(make_snap(10000));
    const double b0 = mm.belief();

    // 10 ticks of 200 lots of buy pressure → +0.02·200·10 = +40 ticks.
    for (Tick t = 2; t <= 11; ++t)
        mm.on_market_data(make_snap(10000, t, 200.0));

    CHECK(mm.belief() - b0 == Catch::Approx(40.0).margin(0.5));

    MarketMakerAS sell(2, "T", rng.for_consumer("sell"), p);
    sell.on_market_data(make_snap(10000));
    for (Tick t = 2; t <= 11; ++t)
        sell.on_market_data(make_snap(10000, t, -200.0));
    CHECK(sell.belief() < b0);
}

TEST_CASE("MarketMakerAS: belief is pulled toward the executed price", "[mm_bayesian]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.lambda_kyle  = 0.0;
    p.belief_decay = 0.5;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    mm.on_market_data(make_snap(10000));
    CHECK(mm.belief() == Catch::Approx(10000.0));

    // Trades printing 20 ticks above the belief drag it halfway each tick.
    mm.on_market_data(make_snap(10000, 2, 0.0, 0.0, /*last=*/10020));
    CHECK(mm.belief() == Catch::Approx(10010.0));
    mm.on_market_data(make_snap(10000, 3, 0.0, 0.0, /*last=*/10020));
    CHECK(mm.belief() == Catch::Approx(10015.0));
}

TEST_CASE("MarketMakerAS: a public announcement reprices the belief", "[mm_bayesian]") {
    // News is public.  A maker that ignores it stands still with stale quotes
    // and is picked off by whoever traded on the announcement.
    RngService rng(42);
    EventBus bus;
    MarketMakerAS::Params p;
    p.lambda_kyle  = 0.0;
    p.belief_decay = 0.0;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p,
                     RiskLimits{}, LatencyProfile{}, InformationProfile{}, &bus);
    mm.on_market_data(make_snap(10000));
    REQUIRE(mm.belief() == Catch::Approx(10000.0));

    NewsEvent ev;
    ev.announce_tick     = 2;
    ev.ticker            = "T";
    ev.impact_log_return = 0.01;      // +1%
    bus.publish(ev);

    auto acts = mm.on_market_data(make_snap(10000, 2));
    CHECK(mm.belief() == Catch::Approx(10000.0 * std::exp(0.01)).epsilon(1e-9));
    // And it quotes there, above the stale observable mid.
    CHECK(quote(acts, Side::Buy).price > 10000);
}

TEST_CASE("MarketMakerAS: belief stays within a band of the observable mid",
          "[mm_bayesian]") {
    // Unbounded flow must not walk the belief to a nonsensical level.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.lambda_kyle  = 0.05;
    p.belief_decay = 0.0;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    for (Tick t = 1; t <= 5000; ++t)
        mm.on_market_data(make_snap(10000, t, 500.0));

    CHECK(mm.belief() <= 11000.0);
    CHECK(mm.belief() >= 10000.0);
}
