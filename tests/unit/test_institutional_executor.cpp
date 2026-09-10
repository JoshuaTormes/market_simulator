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
