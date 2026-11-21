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

TEST_CASE("MarketMakerAS: impact is concave in the size of the flow", "[mm_bayesian]") {
    // With impact_q_ref = 50, a 50-lot imbalance moves the belief by
    // lambda·50 exactly as the linear rule does, and a 200-lot one by only
    // twice that (sqrt(4) = 2), not four times.
    RngService rng(42);
    MarketMakerAS::Params p;
    p.lambda_kyle               = 0.02;
    p.belief_decay              = 0.0;
    p.adverse_sel_ticks_per_lot = 0.0;
    p.impact_q_ref              = 50.0;

    MarketMakerAS a(1, "T", rng.for_consumer("a"), p);
    a.on_market_data(make_snap(10000));
    const double b0 = a.belief();
    a.on_market_data(make_snap(10000, 2, 50.0));
    CHECK(a.belief() - b0 == Catch::Approx(1.0).margin(1e-9));

    MarketMakerAS b(2, "T", rng.for_consumer("b"), p);
    b.on_market_data(make_snap(10000));
    b.on_market_data(make_snap(10000, 2, 200.0));
    CHECK(b.belief() - b0 == Catch::Approx(2.0).margin(1e-9));

    MarketMakerAS c(3, "T", rng.for_consumer("c"), p);
    c.on_market_data(make_snap(10000));
    c.on_market_data(make_snap(10000, 2, -200.0));
    CHECK(c.belief() - b0 == Catch::Approx(-2.0).margin(1e-9));
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

TEST_CASE("MarketMakerAS: a slow maker prices all the flow since its last requote",
          "[mm_bayesian]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.lambda_kyle               = 0.02;
    p.belief_decay              = 0.0;
    p.adverse_sel_ticks_per_lot = 0.0;
    p.impact_q_ref              = 0.0;   // linear rule: the push is λ·ΣOFI
    p.requote_period            = 3;
    p.requote_phase             = 0;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    mm.on_market_data(make_snap(10000, 0, 0.0));         // first tick: belief = mid
    const double b0 = mm.belief();
    CHECK(mm.on_market_data(make_snap(10000, 1, 10.0)).empty());   // off cadence
    CHECK(mm.on_market_data(make_snap(10000, 2, 10.0)).empty());   // off cadence
    CHECK(!mm.on_market_data(make_snap(10000, 3, 10.0)).empty());  // requote
    CHECK(mm.belief() - b0 == Catch::Approx(0.02 * 30.0).margin(1e-9));
}

TEST_CASE("MarketMakerAS: random cadence keeps the mean period and outlives each gap",
          "[mm_bayesian]") {
    RngService rng(42);
    MarketMakerAS::Params p;
    p.requote_period = 3;
    p.random_cadence = true;

    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p);
    Tick last = 0, last_ttl = 0;
    int requotes = 0, min_gap = 1 << 30, max_gap = 0;
    const Tick n = 30000;
    for (Tick t = 0; t < n; ++t) {
        const auto acts = mm.on_market_data(make_snap(10000, t));
        if (acts.empty()) continue;
        if (t > 0) {
            const int gap = static_cast<int>(t - last);
            min_gap = std::min(min_gap, gap);
            max_gap = std::max(max_gap, gap);
            // The previous quote is still resting when its successor arrives.
            CHECK(last_ttl > t);
        }
        last = t;
        last_ttl = quote(acts, Side::Buy).ttl_expiry;
        ++requotes;
    }
    CHECK(min_gap == 1);
    CHECK(max_gap == 5);                                  // 2·period − 1
    CHECK(double(n) / requotes == Catch::Approx(3.0).margin(0.1));
}

// Belief after one +1% announcement, for a given news-reading mode.
static double belief_after_news(double noise, bool blind, uint64_t news_seed) {
    RngService rng(42);
    EventBus bus;
    MarketMakerAS::Params p;
    p.lambda_kyle       = 0.0;
    p.belief_decay      = 0.0;
    p.news_signal_noise = noise;
    p.news_blind        = blind;
    MarketMakerAS mm(1, "T", rng.for_consumer("mm"), p,
                     RiskLimits{}, LatencyProfile{}, InformationProfile{}, &bus);
    mm.set_news_rng(RngService(news_seed).for_consumer("mmn"));
    mm.on_market_data(make_snap(10000));
    NewsEvent ev;
    ev.announce_tick     = 2;
    ev.ticker            = "T";
    ev.impact_log_return = 0.01;
    bus.publish(ev);
    mm.on_market_data(make_snap(10000, 2));
    return mm.belief();
}

TEST_CASE("MarketMakerAS: noisy and blind readings of an announcement", "[mm_bayesian]") {
    // s = 0 is the exact reading, whatever the noise stream holds.
    CHECK(belief_after_news(0.0, false, 1) == 10000.0 * std::exp(0.01));
    CHECK(belief_after_news(0.0, false, 7) == 10000.0 * std::exp(0.01));
    // Blind: the announcement does not move the belief.
    CHECK(belief_after_news(0.5, true, 1) == 10000.0);
    // s > 0: the reading is off, deterministically per stream.
    const double a = belief_after_news(0.5, false, 1);
    CHECK(a != Catch::Approx(10000.0 * std::exp(0.01)).epsilon(1e-6));
    CHECK(a == belief_after_news(0.5, false, 1));
    // Across streams the reading is centred on the announcement.
    double sum = 0.0;
    const int n = 2000;
    for (int k = 0; k < n; ++k) sum += std::log(belief_after_news(0.5, false, 100 + k) / 10000.0);
    CHECK(sum / n == Catch::Approx(0.01).margin(0.0005));
}
