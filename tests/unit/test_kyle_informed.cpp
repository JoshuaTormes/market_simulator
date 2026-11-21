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

// Deterministic signal: no observation noise, no heavy tail, unit speed so a
// single tick reveals the target.
static InformedTraderKyle::Params clean_params() {
    InformedTraderKyle::Params p;
    p.signal_noise_log     = 0.0;
    p.pareto_alpha         = 0.0;
    p.target_lots_per_tick = 0.5;
    p.speed                = 1.0;
    p.deadband_sigma       = 1.0;
    p.margin_ticks         = 1.0;
    p.min_order            = 1;
    p.max_order_size       = 100000;
    return p;
}

static RiskLimits wide_limits() {
    RiskLimits rl;
    rl.max_position = 1'000'000;
    return rl;
}

static SubmitOrder only_order(const std::vector<Action>& acts) {
    REQUIRE(acts.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&acts[0]);
    REQUIRE(so != nullptr);
    return *so;
}

TEST_CASE("InformedTraderKyle: buys when the fundamental is above the mid", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params(), wide_limits());

    // Gap 100, spread 2 → threshold 2 → effective gap 98 → target 49 lots.
    auto so = only_order(it.on_market_data(make_snap(10000, 10100.0)));
    CHECK(so.side == Side::Buy);
    CHECK(so.type == OrderType::Market);
    CHECK(so.qty  == 49);
    CHECK(it.target() == Catch::Approx(49.0));
}

TEST_CASE("InformedTraderKyle: sells when the fundamental is below the mid", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params(), wide_limits());
    auto so = only_order(it.on_market_data(make_snap(10000, 9900.0)));
    CHECK(so.side == Side::Sell);
    CHECK(so.qty  == 49);
}

TEST_CASE("InformedTraderKyle: does not pay a spread wider than the edge", "[kyle]") {
    // A 3-tick gap is real information, but not worth crossing a 20-tick
    // spread.  Trading it anyway is how the old version bled P&L into the
    // market makers.
    RngService rng(42);
    auto p = clean_params();
    p.target_lots_per_tick = 40.0;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    CHECK(it.on_market_data(make_snap(10000, 10003.0, /*spread=*/20)).empty());
    CHECK(it.target() == 0.0);
    // The same gap is worth taking when the book is tight.
    CHECK(it.on_market_data(make_snap(10000, 10003.0, /*spread=*/2)).size() == 1);
}

TEST_CASE("InformedTraderKyle: a gap inside the own signal error is not traded", "[kyle]") {
    // With a 0.1% signal noise at $100 one sigma is 10 ticks: a 6-tick gap is
    // inside the error bar and the trader stays out, deadband or no spread.
    RngService rng(42);
    auto p = clean_params();
    p.signal_noise_log = 1e-3;
    p.deadband_sigma   = 1.0;
    p.margin_ticks     = 0.0;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    // The noise draw is live here, so look at the target through many ticks
    // with V exactly at the mid: |ε·V| exceeds 10 ticks only ~32% of the time
    // and the target must be zero on the rest.
    int zero = 0;
    for (int i = 0; i < 400; ++i) {
        it.on_market_data(make_snap(10000, 10000.0, 0));
        if (it.target() == 0.0) ++zero;
    }
    CHECK(zero > 200);
    CHECK(zero < 360);
}

TEST_CASE("InformedTraderKyle: moves toward the target at the configured speed", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.speed = 0.25;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    // Target 49, flat → first slice is a quarter of the way: 12 lots.
    auto so = only_order(it.on_market_data(make_snap(10000, 10100.0)));
    CHECK(so.side == Side::Buy);
    CHECK(so.qty  == 12);

    // Already long 40 → only 9 to go → slice of 2.
    auto so2 = only_order(it.on_market_data(make_snap(10000, 10100.0, 2, 40.0)));
    CHECK(so2.side == Side::Buy);
    CHECK(so2.qty  == 2);
}

TEST_CASE("InformedTraderKyle: comes back to flat once the gap closes", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.speed = 0.1;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    // Fair price, long 500 lots → target 0 → sell 50 back this tick.
    auto so = only_order(it.on_market_data(make_snap(10000, 10000.0, 2, /*own_inv=*/500.0)));
    CHECK(so.side == Side::Sell);
    CHECK(so.qty  == 50);

    // Short position unwinds by buying.
    auto shrt = only_order(it.on_market_data(make_snap(10000, 10000.0, 2, /*own_inv=*/-200.0)));
    CHECK(shrt.side == Side::Buy);
    CHECK(shrt.qty  == 20);

    // Flat and fairly priced → nothing to do.
    CHECK(it.on_market_data(make_snap(10000, 10000.0, 2, 0.0)).empty());
}

TEST_CASE("InformedTraderKyle: slices below min_order are not sent", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.speed     = 0.1;
    p.min_order = 5;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    // Long 30, target 0 → slice of 3 < 5: stay put rather than pay the spread.
    CHECK(it.on_market_data(make_snap(10000, 10000.0, 2, 30.0)).empty());
    // Long 80 → slice of 8: sent.
    CHECK(only_order(it.on_market_data(make_snap(10000, 10000.0, 2, 80.0))).qty == 8);
}

TEST_CASE("InformedTraderKyle: order size and target are capped", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    p.max_order_size       = 200;
    p.target_lots_per_tick = 40.0;
    p.q_soft_frac          = 0.5;
    RiskLimits rl; rl.max_position = 4000;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, rl);

    // A 5000-tick gap would ask for ~200k lots; the target stops at
    // q_soft = 2000 and the slice at 200.
    auto so = only_order(it.on_market_data(make_snap(10000, 15000.0)));
    CHECK(so.qty == 200);
    CHECK(it.target() == Catch::Approx(2000.0));
}

TEST_CASE("InformedTraderKyle: bigger gaps mean bigger positions", "[kyle]") {
    RngService rng(42);
    auto p = clean_params();
    InformedTraderKyle small(1, "T", rng.for_consumer("s"), p, wide_limits());
    InformedTraderKyle large(2, "T", rng.for_consumer("l"), p, wide_limits());

    auto a_small = only_order(small.on_market_data(make_snap(10000, 10020.0)));
    auto a_large = only_order(large.on_market_data(make_snap(10000, 10200.0)));
    CHECK(a_large.qty > a_small.qty);
    CHECK(large.target() > small.target());
}

TEST_CASE("InformedTraderKyle: the signal error is proportional to the price", "[kyle]") {
    // signal_noise_log is a log-noise, so the same sigma gives the same
    // relative signal quality at $1 and at $1000.
    RngService rng(7);
    auto p = clean_params();
    p.signal_noise_log = 0.01;
    p.deadband_sigma   = 0.0;
    p.margin_ticks     = 0.0;
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), p, wide_limits());

    // With V exactly at the mid, the noise alone decides the side, so both
    // sides must appear over many draws.
    int buys = 0, sells = 0;
    for (int i = 0; i < 200; ++i) {
        auto acts = it.on_market_data(make_snap(10000, 10000.0, 0));
        if (acts.empty()) continue;
        if (std::get<SubmitOrder>(acts[0]).side == Side::Buy) ++buys; else ++sells;
    }
    CHECK(buys  > 20);
    CHECK(sells > 20);
}

TEST_CASE("InformedTraderKyle: RNG stream does not depend on the state", "[kyle][determinism]") {
    // Two traders with the same seed see different states (one has no
    // fundamental for a while) and must still emit the same order once their
    // inputs coincide: every draw happens every tick.
    RngService rng(42);
    auto p = clean_params();
    p.signal_noise_log = 1e-3;
    p.pareto_alpha     = 1.5;
    InformedTraderKyle a(1, "T", rng.for_consumer("x"), p, wide_limits());
    InformedTraderKyle b(2, "T", rng.for_consumer("x"), p, wide_limits());

    AgentSnapshot blind = make_snap(10000, 10100.0);
    blind.has_fundamental = false;
    for (int i = 0; i < 10; ++i) {
        a.on_market_data(blind);
        b.on_market_data(make_snap(10000, 10100.0));
    }
    auto sa = only_order(a.on_market_data(make_snap(10000, 10100.0)));
    auto sb = only_order(b.on_market_data(make_snap(10000, 10100.0)));
    CHECK(sa.qty == sb.qty);
}

TEST_CASE("InformedTraderKyle: no fundamental means no action", "[kyle]") {
    RngService rng(42);
    InformedTraderKyle it(1, "T", rng.for_consumer("it"), clean_params(), wide_limits());
    AgentSnapshot as = make_snap(10000, 10500.0);
    as.has_fundamental = false;
    CHECK(it.on_market_data(as).empty());
}
