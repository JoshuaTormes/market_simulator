#include <catch_amalgamated.hpp>
#include "agents/StopLossCluster.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"

static AgentSnapshot make_snap(Price mid, double own_inv = 0.0, double own_avg = 0.0) {
    AgentSnapshot as;
    as.base.tick         = 1;
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

TEST_CASE("StopLossCluster: fires only once (triggered flag)", "[stop_loss]") {
    RngService rng(42);
    StopLossCluster::Params p;
    p.trigger_pct = 0.03;
    p.qty         = 50;

    StopLossCluster sl(1, "T", rng.for_consumer("sl"), p);

    // First call triggers
    REQUIRE(sl.on_market_data(make_snap(9500, 50.0, 10000.0)).size() == 1);
    // Second call must be empty — triggered_ flag prevents double-fire
    CHECK(sl.on_market_data(make_snap(9000, 50.0, 10000.0)).empty());
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

    // No ledger data (own_inv=0, own_avg=0) but mid < stop_level → fallback fires
    auto actions = sl.on_market_data(make_snap(9600, 0.0, 0.0));
    REQUIRE(actions.size() == 1);
    CHECK(std::get<SubmitOrder>(actions[0]).side == Side::Sell);
    CHECK(std::get<SubmitOrder>(actions[0]).qty  == 40);  // uses params.qty
}
