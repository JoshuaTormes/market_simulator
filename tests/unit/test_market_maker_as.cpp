#include <catch_amalgamated.hpp>
#include "agents/MarketMakerAS.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"
#include <cmath>

// Quote geometry and the requote cycle.  Belief dynamics live in
// test_market_maker_bayesian.cpp.

static AgentSnapshot make_snap(Price mid, Tick tick = 1,
                               double ofi = 0.0, double own_inv = 0.0,
                               double rv = 0.0) {
    AgentSnapshot as;
    as.base.tick             = tick;
    as.base.mid_price        = mid;
    as.base.spread           = 2;
    as.base.last_trade_price = mid;
    as.base.realized_vol[0]  = rv;
    as.base.ofi_tick         = ofi;
    as.perceived_mid         = mid;
    as.perceived_last        = mid;
    as.own_inventory         = own_inv;
    return as;
}

// Actions are always [CancelAll, bid?, ask?].
static const SubmitOrder& quote(const std::vector<Action>& acts, Side side) {
    for (const auto& a : acts)
        if (const auto* so = std::get_if<SubmitOrder>(&a))
            if (so->side == side) return *so;
    throw std::runtime_error("no quote on that side");
}

TEST_CASE("MarketMakerAS: requotes both sides every tick", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), {});
    auto acts = mm.on_market_data(make_snap(10000));

    REQUIRE(acts.size() == 3);
    // The mass cancel must come first: at equal arrival ticks only its lower
    // seq keeps it from wiping the quotes that follow it.
    CHECK(std::holds_alternative<CancelAll>(acts[0]));
    CHECK(quote(acts, Side::Buy).side  == Side::Buy);
    CHECK(quote(acts, Side::Sell).side == Side::Sell);
}

TEST_CASE("MarketMakerAS: bid < mid < ask with a one-tick floor", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.sigma_tick_floor = 0.0;        // isolate the floor term
    p.vol_mult         = 0.0;
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);

    auto acts = mm.on_market_data(make_snap(10000));
    Price bid = quote(acts, Side::Buy).price;
    Price ask = quote(acts, Side::Sell).price;

    CHECK(bid < 10000);
    CHECK(ask > 10000);
    CHECK(ask - bid >= 2);           // one tick each side of the reservation
}

TEST_CASE("MarketMakerAS: realized volatility widens the spread", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.vol_mult = 1.0;

    MarketMakerAS quiet(1, "T", rng.for_consumer("quiet"), p);
    MarketMakerAS loud (2, "T", rng.for_consumer("loud"),  p);

    auto aq = quiet.on_market_data(make_snap(10000, 1, 0.0, 0.0, 0.0002));
    auto al = loud .on_market_data(make_snap(10000, 1, 0.0, 0.0, 0.0050));

    Price sq = quote(aq, Side::Sell).price - quote(aq, Side::Buy).price;
    Price sl = quote(al, Side::Sell).price - quote(al, Side::Buy).price;
    CHECK(sl > sq);
}

TEST_CASE("MarketMakerAS: toxic flow widens the spread", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.adverse_sel_ticks_per_lot = 0.05;

    MarketMakerAS clean(1, "T", rng.for_consumer("clean"), p);
    MarketMakerAS toxic(2, "T", rng.for_consumer("toxic"), p);

    auto ac = clean.on_market_data(make_snap(10000, 1,   0.0));
    auto at = toxic.on_market_data(make_snap(10000, 1, 200.0));

    Price sc = quote(ac, Side::Sell).price - quote(ac, Side::Buy).price;
    Price st = quote(at, Side::Sell).price - quote(at, Side::Buy).price;
    CHECK(st > sc);
}

TEST_CASE("MarketMakerAS: the growing side fades to zero at q_soft", "[mm_as]") {
    // Price skew alone never stops a maker from accumulating; size does.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.q_soft = 300.0;
    p.qty    = 100;

    MarketMakerAS flat(1, "T", rng.for_consumer("flat"), p);
    MarketMakerAS half(2, "T", rng.for_consumer("half"), p);
    MarketMakerAS full(3, "T", rng.for_consumer("full"), p);

    auto a0 = flat.on_market_data(make_snap(10000, 1, 0.0,   0.0));
    auto a1 = half.on_market_data(make_snap(10000, 1, 0.0, 150.0));
    auto a2 = full.on_market_data(make_snap(10000, 1, 0.0, 300.0));

    CHECK(quote(a0, Side::Buy).qty  == 100);
    CHECK(quote(a1, Side::Buy).qty  == 50);   // half faded
    CHECK(quote(a1, Side::Sell).qty == 100);  // reducing side untouched
    // At q_soft the bid disappears entirely; only the ask is quoted.
    CHECK(a2.size() == 2);
    CHECK(quote(a2, Side::Sell).qty == 100);

    // Short inventory is the mirror image.
    MarketMakerAS shrt(4, "T", rng.for_consumer("short"), p);
    auto a3 = shrt.on_market_data(make_snap(10000, 1, 0.0, -300.0));
    CHECK(a3.size() == 2);
    CHECK(quote(a3, Side::Buy).qty == 100);
}

TEST_CASE("MarketMakerAS: quotes carry TTL = tick + 3", "[mm_as]") {
    // The cancel is the primary mechanism and the TTL only a net, but it must
    // clear the requote cycle: the agent sees tick t-1, the order lands at t,
    // and expire(t) runs before the snapshot is published, so anything tighter
    // than t+1 deletes the quote before the book is ever observed.
    RngService rng(42);
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), {});
    auto acts = mm.on_market_data(make_snap(10000, 50));

    for (const auto& a : acts)
        if (const auto* so = std::get_if<SubmitOrder>(&a))
            CHECK(so->ttl_expiry == 53);
}

TEST_CASE("MarketMakerAS: no observable mid means no quotes", "[mm_as]") {
    RngService rng(42);
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), {});
    CHECK(mm.on_market_data(make_snap(0)).empty());
}
