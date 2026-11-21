#include <catch_amalgamated.hpp>
#include "agents/InstitutionalExecutor.h"
#include "core/RngService.h"
#include <cmath>
#include <map>

static AgentSnapshot snap_at(Tick t) {
    AgentSnapshot as;
    as.base.tick      = t;
    as.base.spread    = 2;
    as.base.mid_price = 10000;
    as.perceived_mid  = 10000;
    return as;
}

TEST_CASE("InstitutionalExecutor: parents keep arriving over the run",
          "[institutional]") {
    // The old executor was handed one parent at construction and went silent
    // for the rest of the session.  With an arrival process it must still be
    // trading late in the run.
    RngService rng(42);
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 0.01;
    p.total_slices   = 20;
    p.ticks_between  = 5;

    InstitutionalExecutor ie(1, "T", rng.for_consumer("ie"), p);

    int orders_early = 0, orders_late = 0;
    for (Tick t = 0; t < 20'000; ++t) {
        const size_t n = ie.on_market_data(snap_at(t)).size();
        if (t < 5'000)       orders_early += static_cast<int>(n);
        else if (t >= 15'000) orders_late  += static_cast<int>(n);
    }

    CHECK(ie.parents_started() > 5);
    CHECK(orders_early > 0);
    CHECK(orders_late  > 0);
}

TEST_CASE("InstitutionalExecutor: both directions are worked", "[institutional]") {
    // A desk fixed to one side is a permanent directional push, not execution.
    RngService rng(7);
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 0.02;

    InstitutionalExecutor ie(1, "T", rng.for_consumer("ie"), p);
    std::map<Side, int> by_side;
    for (Tick t = 0; t < 30'000; ++t)
        for (const Action& a : ie.on_market_data(snap_at(t)))
            ++by_side[std::get<SubmitOrder>(a).side];

    CHECK(by_side[Side::Buy]  > 0);
    CHECK(by_side[Side::Sell] > 0);
}

TEST_CASE("InstitutionalExecutor: children are spaced and capped", "[institutional]") {
    RngService rng(3);
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 1.0;      // a parent on the very first tick
    p.parent_min     = 2000;
    p.parent_max     = 2000;     // degenerate Pareto: exactly 2000 lots
    p.total_slices   = 10;
    p.ticks_between  = 5;
    p.max_child_qty  = 200;

    InstitutionalExecutor ie(1, "T", rng.for_consumer("ie"), p);

    // 10 children spaced 5 ticks apart occupy ticks 0..45; the window stops
    // before tick 50, where the next parent would arrive.
    std::vector<Tick> fire_ticks;
    for (Tick t = 0; t < 50; ++t)
        for (const Action& a : ie.on_market_data(snap_at(t))) {
            fire_ticks.push_back(t);
            CHECK(std::get<SubmitOrder>(a).qty <= 200);
            CHECK(std::get<SubmitOrder>(a).type == OrderType::Market);
        }

    REQUIRE(fire_ticks.size() == 10);          // exactly total_slices children
    for (size_t i = 1; i < fire_ticks.size(); ++i)
        CHECK(fire_ticks[i] - fire_ticks[i-1] == 5);
}

TEST_CASE("InstitutionalExecutor: parent sizes are heavy-tailed and truncated",
          "[institutional]") {
    // Pareto(200, 1.5) truncated at 3000: the median sits near 320 lots while
    // the largest draws pin at the cap.  That spread is the source of the
    // heavy-tailed trade flow.
    RngService rng(11);
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 1.0;
    p.total_slices   = 1;        // one child per parent → child qty == parent qty
    p.ticks_between  = 1;
    p.pareto_alpha   = 0.0;      // no child multiplier
    p.max_child_qty  = 3000;
    p.parent_min     = 200;
    p.parent_max     = 3000;
    p.parent_alpha   = 1.5;

    InstitutionalExecutor ie(1, "T", rng.for_consumer("ie"), p);
    std::vector<Qty> sizes;
    for (Tick t = 0; t < 4000; ++t)
        for (const Action& a : ie.on_market_data(snap_at(t)))
            sizes.push_back(std::get<SubmitOrder>(a).qty);

    REQUIRE(sizes.size() > 1000);
    Qty lo = sizes[0], hi = sizes[0];
    for (Qty q : sizes) { lo = std::min(lo, q); hi = std::max(hi, q); }
    CHECK(lo >= 200);
    CHECK(hi <= 3000);
    CHECK(hi >  1000);   // the tail is actually reached
}

TEST_CASE("InstitutionalExecutor: same seed → same order sequence",
          "[institutional][determinism]") {
    RngService rng1(99), rng2(99);
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 0.05;

    InstitutionalExecutor a(1, "T", rng1.for_consumer("ie"), p);
    InstitutionalExecutor b(1, "T", rng2.for_consumer("ie"), p);

    for (Tick t = 0; t < 5000; ++t) {
        auto aa = a.on_market_data(snap_at(t));
        auto bb = b.on_market_data(snap_at(t));
        REQUIRE(aa.size() == bb.size());
        for (size_t i = 0; i < aa.size(); ++i) {
            CHECK(std::get<SubmitOrder>(aa[i]).side == std::get<SubmitOrder>(bb[i]).side);
            CHECK(std::get<SubmitOrder>(aa[i]).qty  == std::get<SubmitOrder>(bb[i]).qty);
        }
    }
}

// Schema v4: each finished parent is published as a MetaorderEvent, closed one
// call after its last child so that child's fill is in own_inventory.  The
// bookkeeping must not touch the RNG: with and without a bus the desk emits
// exactly the same orders.
TEST_CASE("InstitutionalExecutor: metaorders are published without moving the RNG",
          "[institutional]") {
    InstitutionalExecutor::Params p;
    p.arrival_lambda = 0.01;
    p.total_slices   = 10;
    p.ticks_between  = 3;

    RngService rng_a(11), rng_b(11);
    EventBus bus;
    std::vector<MetaorderEvent> got;
    bus.subscribe<MetaorderEvent>([&](const MetaorderEvent& m) { got.push_back(m); });

    InstitutionalExecutor plain(1, "T", rng_a.for_consumer("ie"), p);
    InstitutionalExecutor wired(1, "T", rng_b.for_consumer("ie"), p,
                                RiskLimits{}, LatencyProfile{}, InformationProfile{}, &bus);

    // Every child fills in full; the fill shows up in the next snapshot.
    double inv = 0.0;
    Qty submitted_total = 0;
    for (Tick t = 0; t < 20'000; ++t) {
        AgentSnapshot s = snap_at(t);
        s.own_inventory = inv;
        auto a = plain.on_market_data(s);
        auto b = wired.on_market_data(s);
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            const auto& sa = std::get<SubmitOrder>(a[i]);
            const auto& sb = std::get<SubmitOrder>(b[i]);
            REQUIRE(sa.side == sb.side);
            REQUIRE(sa.qty  == sb.qty);
            inv += (sb.side == Side::Buy ? 1.0 : -1.0) * static_cast<double>(sb.qty);
            submitted_total += sb.qty;
        }
    }

    REQUIRE(got.size() >= 5);
    Qty published_total = 0;
    for (const auto& m : got) {
        CHECK(m.agent == 1);
        CHECK(m.tick_end >= m.tick_start);
        CHECK(m.qty_submitted <= m.qty_requested);
        CHECK(m.qty_executed  == m.qty_submitted);   // every child filled
        published_total += m.qty_submitted;
    }
    // Only a parent still open at the end of the run is missing.
    CHECK(published_total <= submitted_total);
    CHECK(submitted_total - published_total <= p.parent_max);
}
