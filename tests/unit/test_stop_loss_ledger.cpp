#include <catch_amalgamated.hpp>
#include "agents/StopLossCluster.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"

static AgentSnapshot make_snap(Price mid, double own_inv = 0.0, double own_avg = 0.0,
                               Tick tick = 1) {
    AgentSnapshot as;
    as.base.tick         = tick;
    as.base.spread       = 2;
    as.base.mid_price    = mid;
    as.perceived_mid     = mid;
    as.own_inventory     = own_inv;
    as.own_avg_cost      = own_avg;
    return as;
}

TEST_CASE("StopLossCluster: ledger long position triggers on price drop", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.trigger_pct  = 0.03;  // 3% stop
    p.qty          = 50;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // Long 50 lots at avg cost 10000, mid drops 5% → breaches 3% stop
    auto actions = sl.on_market_data(make_snap(9500, 50.0, 10000.0));
    REQUIRE(actions.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&actions[0]);
    REQUIRE(so != nullptr);
    CHECK(so->side == Side::Sell);
    CHECK(so->qty  == 50);   // liquidates full ledger position
    CHECK(so->type == OrderType::Market);
}

TEST_CASE("StopLossCluster: does not trigger when loss within threshold", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.trigger_pct  = 0.03;
    p.qty          = 50;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // Mid dropped only 1% — within the 3% stop, should not fire
    auto actions = sl.on_market_data(make_snap(9900, 50.0, 10000.0));
    CHECK(actions.empty());
}

TEST_CASE("StopLossCluster: ledger short position triggers on upside breach", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.initial_side = Side::Sell;
    p.trigger_pct  = 0.03;
    p.qty          = 30;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // Short 30 lots at 10000, mid rises 5% → breaches 3% upside stop
    auto actions = sl.on_market_data(make_snap(10500, -30.0, 10000.0));
    REQUIRE(actions.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&actions[0]);
    REQUIRE(so != nullptr);
    CHECK(so->side == Side::Buy);   // buy to cover short
    CHECK(so->qty  == 30);
}

TEST_CASE("StopLossCluster: fires once per position, not once per run", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.trigger_pct    = 0.03;
    p.qty            = 50;
    p.cooldown_ticks = 100;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // First breach liquidates.
    REQUIRE(sl.on_market_data(make_snap(9500, 50.0, 10000.0, 1)).size() == 1);
    // While flat and inside the cooldown the cluster is silent, even as the
    // price keeps falling — the position is already gone.
    CHECK(sl.on_market_data(make_snap(9000, 0.0, 0.0, 2)).empty());
    CHECK(sl.on_market_data(make_snap(9000, 0.0, 0.0, 100)).empty());
}

TEST_CASE("StopLossCluster: re-enters after the cooldown", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.trigger_pct    = 0.03;
    p.qty            = 50;
    p.cooldown_ticks = 100;
    p.qty_min        = 10;
    p.qty_max        = 75;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);
    REQUIRE(sl.on_market_data(make_snap(9500, 50.0, 10000.0, 1)).size() == 1);
    REQUIRE(sl.stops_fired() == 1);

    // One tick past the cooldown a fresh position is opened at market.
    auto entry = sl.on_market_data(make_snap(9000, 0.0, 0.0, 101));
    REQUIRE(entry.size() == 1);
    auto* so = std::get_if<SubmitOrder>(&entry[0]);
    REQUIRE(so != nullptr);
    CHECK(so->type == OrderType::Market);
    CHECK(so->qty >= 10);
    CHECK(so->qty <= 75);
}

TEST_CASE("StopLossCluster: the new stop is measured from the new entry", "[stop_loss]") {
    // The re-entry reference is the mid at entry, not the original 10000, so a
    // cluster re-armed at 9000 stops out around 8730 rather than immediately.
    RngService rng(3);
    StopLossCluster::Params p;
    p.initial_side   = Side::Buy;
    p.entry_price    = 10000;
    p.trigger_pct    = 0.03;
    p.qty            = 50;
    p.cooldown_ticks = 10;
    p.qty_min = 40; p.qty_max = 40;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);
    REQUIRE(sl.on_market_data(make_snap(9500, 50.0, 10000.0, 1)).size() == 1);

    auto entry = sl.on_market_data(make_snap(9000, 0.0, 0.0, 11));
    REQUIRE(entry.size() == 1);
    const Side entered = std::get<SubmitOrder>(entry[0]).side;

    // A 1% move against the new position is inside the 3% stop.
    const Price mild = (entered == Side::Buy) ? 8910 : 9090;
    CHECK(sl.on_market_data(make_snap(mild, 0.0, 0.0, 12)).empty());

    // A 5% move against it breaches.
    const Price hard = (entered == Side::Buy) ? 8550 : 9450;
    auto exit_act = sl.on_market_data(make_snap(hard, 0.0, 0.0, 13));
    REQUIRE(exit_act.size() == 1);
    CHECK(std::get<SubmitOrder>(exit_act[0]).side
          != entered);              // exits in the opposite direction
    CHECK(sl.stops_fired() == 2);
}

TEST_CASE("StopLossCluster: the ledger reference wins once the fill lands", "[stop_loss]") {
    // Between the entry order and its fill the agent only knows the mid it
    // aimed at; once the ledger shows a position on the expected side, the real
    // average cost takes over.  A fill 2% worse than the aimed price must move
    // the stop with it.
    RngService rng(42);
    StopLossCluster::Params p;
    p.initial_side = Side::Buy;
    p.entry_price  = 10000;
    p.trigger_pct  = 0.03;
    p.qty          = 50;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);
    // Filled long at 9800: the stop sits at 9506, so 9600 is safe...
    CHECK(sl.on_market_data(make_snap(9600, 50.0, 9800.0, 1)).empty());
    // ...and 9500 is not.
    CHECK(sl.on_market_data(make_snap(9500, 50.0, 9800.0, 2)).size() == 1);
}

TEST_CASE("StopLossCluster: fallback path when no ledger position", "[stop_loss]") {
    // own_inventory == 0 → falls back to entry_price-based stop_level
    RngService rng(42);
    StopLossCluster::Params p;
    p.initial_side = Side::Buy;
    p.entry_price  = 10000;
    p.trigger_pct  = 0.03;  // stop_level = 9700
    p.qty          = 40;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // No ledger data (own_inv=0, own_avg=0) but mid below entry × (1 - 3%) →
    // the recorded entry reference fires the stop.
    auto actions = sl.on_market_data(make_snap(9600, 0.0, 0.0));
    REQUIRE(actions.size() == 1);
    CHECK(std::get<SubmitOrder>(actions[0]).side == Side::Sell);
    CHECK(std::get<SubmitOrder>(actions[0]).qty  == 40);  // uses params.qty
}
