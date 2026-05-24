#include <catch_amalgamated.hpp>
#include "agents/MarketMakerAS.h"
#include "marketdata/AgentSnapshot.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot make_snap(Price mid, Price spread, Tick tick = 1,
                               double ofi = 0.0, double own_inv = 0.0) {
    AgentSnapshot as;
    as.base.tick             = tick;
    as.base.mid_price        = mid;
    as.base.spread           = spread;
    as.base.last_trade_price = mid;
    as.base.realized_vol[0]  = 0.01;
    as.base.realized_vol[1]  = 0.01;
    as.base.realized_vol[2]  = 0.01;
    as.base.ofi_tick         = ofi;
    as.perceived_mid         = mid;
    as.perceived_last        = mid;
    as.own_inventory         = own_inv;
    return as;
}

TEST_CASE("MarketMakerAS Bayesian: long inventory shifts quotes down", "[mm_bayesian]") {
    // A-S: reservation = belief - q*γ*σ²*(T-t). With q > 0 the reservation drops,
    // pushing both bid and ask below the neutral (q=0) quotes.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 10;

    MarketMakerAS mm_flat(1, "T", rng.for_consumer("mm_flat"), p);
    MarketMakerAS mm_long(2, "T", rng.for_consumer("mm_long"), p);

    auto a_flat = mm_flat.on_market_data(make_snap(10000, 2, 1, 0.0,  0.0));
    auto a_long = mm_long.on_market_data(make_snap(10000, 2, 1, 0.0, 50.0));

    auto bid_flat = std::get<SubmitOrder>(a_flat[0]).price;
    auto ask_flat = std::get<SubmitOrder>(a_flat[1]).price;
    auto bid_long = std::get<SubmitOrder>(a_long[0]).price;
    auto ask_long = std::get<SubmitOrder>(a_long[1]).price;

    // Long inventory → reservation price drops → both quotes shift down.
    CHECK(bid_long <= bid_flat);
    CHECK(ask_long <= ask_flat);
}

TEST_CASE("MarketMakerAS Bayesian: toxic flow widens spread", "[mm_bayesian]") {
    // adverse_sel * |ofi_tick| is added to half-spread.
    // Strong buy-side OFI (toxic for MM selling) should widen the spread.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 10;
    p.adverse_sel = 0.05;

    MarketMakerAS mm_clean(1, "T", rng.for_consumer("mm_clean"), p);
    MarketMakerAS mm_toxic(2, "T", rng.for_consumer("mm_toxic"), p);

    auto a_clean = mm_clean.on_market_data(make_snap(10000, 2, 1, 0.0,   0.0));
    auto a_toxic = mm_toxic.on_market_data(make_snap(10000, 2, 1, 100.0, 0.0));

    Price spread_clean = std::get<SubmitOrder>(a_clean[1]).price
                       - std::get<SubmitOrder>(a_clean[0]).price;
    Price spread_toxic = std::get<SubmitOrder>(a_toxic[1]).price
                       - std::get<SubmitOrder>(a_toxic[0]).price;

    CHECK(spread_toxic >= spread_clean);
}

TEST_CASE("MarketMakerAS Bayesian: buy OFI raises belief and quotes", "[mm_bayesian]") {
    // beta_ofi > 0: persistent buy pressure pushes belief up, raising reservation price.
    // After several ticks of positive OFI, ask price should exceed the no-OFI case.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 5;
    p.beta_ofi = 0.5; p.belief_decay = 0.001; p.adverse_sel = 0.0;

    MarketMakerAS mm_buy(1, "T", rng.for_consumer("mm_buy"), p);
    MarketMakerAS mm_neu(2, "T", rng.for_consumer("mm_neu"), p);

    // Warm up with 5 ticks of buy pressure vs. neutral OFI.
    Price mid = 10000;
    Price last_ask_buy = 0, last_ask_neu = 0;
    for (int t = 1; t <= 5; ++t) {
        auto ab = mm_buy.on_market_data(make_snap(mid, 2, t, 200.0, 0.0));
        auto an = mm_neu.on_market_data(make_snap(mid, 2, t,   0.0, 0.0));
        if (!ab.empty()) last_ask_buy = std::get<SubmitOrder>(ab[1]).price;
        if (!an.empty()) last_ask_neu = std::get<SubmitOrder>(an[1]).price;
    }

    CHECK(last_ask_buy >= last_ask_neu);
}

TEST_CASE("MarketMakerAS Bayesian: own_inventory filled from snapshot", "[mm_bayesian]") {
    // Sanity: the snapshot's own_inventory field is the mechanism through which
    // AgentRunner wires the real ledger position. Verify the MM reads it correctly
    // by checking that a large long position produces a lower mid-price quote.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.gamma = 0.1; p.kappa = 1.5; p.sigma = 0.02; p.T = 100.0; p.qty = 10;

    Price mid = 10000;
    MarketMakerAS mm_a(1, "T", rng.for_consumer("a"), p);
    MarketMakerAS mm_b(2, "T", rng.for_consumer("b"), p);

    auto aa = mm_a.on_market_data(make_snap(mid, 2, 1, 0.0,   0.0));
    auto ab = mm_b.on_market_data(make_snap(mid, 2, 1, 0.0, 200.0));

    Price mid_a = (std::get<SubmitOrder>(aa[0]).price + std::get<SubmitOrder>(aa[1]).price) / 2;
    Price mid_b = (std::get<SubmitOrder>(ab[0]).price + std::get<SubmitOrder>(ab[1]).price) / 2;

    CHECK(mid_b <= mid_a);
}
