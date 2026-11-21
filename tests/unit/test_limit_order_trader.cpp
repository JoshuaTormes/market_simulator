#include <catch_amalgamated.hpp>
#include "agents/LimitOrderTrader.h"
#include "marketdata/AgentSnapshot.h"
#include "marketdata/MarketDataPublisher.h"
#include "orderbook/OrderBookV2.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot lot_snap(Tick tick, double V, double inv = 0.0,
                              Price bid = 9998, Price ask = 10002) {
    AgentSnapshot as;
    as.base.tick = tick;
    as.base.mid_price = (bid + ask) / 2;
    as.base.spread = ask - bid;
    as.base.bid_levels[0] = {bid, 100, 1};
    as.base.ask_levels[0] = {ask, 100, 1};
    as.base.realized_vol[MarketSnapshot::kWindows - 1] = 1.4e-4;
    as.perceived_mid  = as.base.mid_price;
    as.perceived_last = as.base.mid_price;
    as.fundamental_value = V;
    as.has_fundamental   = V > 0.0;
    as.own_inventory = inv;
    return as;
}

static LimitOrderTrader::Params lot_params() {
    LimitOrderTrader::Params p;
    p.p_act = 1.0;          // act every tick
    p.g_fund = 1.0; p.g_chart = 0.5; p.g_noise = 1.0;
    p.horizon_ticks = 120;
    p.chart_window = 30;
    p.fund_reversion_ticks = 120;
    p.view_sigma = 0.002;
    p.k_max = 0.002;
    p.q_soft = 300.0;
    return p;
}

static const SubmitOrder* first_order(const std::vector<Action>& a) {
    for (const auto& x : a) if (const auto* so = std::get_if<SubmitOrder>(&x)) return so;
    return nullptr;
}

TEST_CASE("LimitOrderTrader: never crosses the touch it sees", "[lot]") {
    RngService rng(7);
    LimitOrderTrader t(1, "T", rng.for_consumer("lot"), lot_params());
    int buys = 0, sells = 0;
    for (Tick k = 1; k <= 5000; ++k) {
        // V swings 3% above and below the mid: strong signals both ways.
        const double V = 10000.0 * (1.0 + 0.03 * std::sin(k / 200.0));
        const auto acts = t.on_market_data(lot_snap(k, V));
        const auto* so = first_order(acts);
        if (!so) continue;
        CHECK(so->type == OrderType::Limit);
        if (so->side == Side::Buy) { ++buys;  CHECK(so->price <= 10001); }
        else                       { ++sells; CHECK(so->price >= 9999); }
    }
    CHECK(buys > 500);
    CHECK(sells > 500);
}

TEST_CASE("LimitOrderTrader: the order rests for the trader's horizon", "[lot]") {
    RngService rng(7);
    auto p = lot_params();
    LimitOrderTrader t(1, "T", rng.for_consumer("lot"), p);
    int seen = 0;
    for (Tick k = 1; k <= 200; ++k) {
        const auto acts = t.on_market_data(lot_snap(k, 10200.0));
        const auto* so = first_order(acts);
        if (!so) continue;
        CHECK(so->ttl_expiry == k + p.horizon_ticks);
        ++seen;
    }
    CHECK(seen > 0);
}

TEST_CASE("LimitOrderTrader: draws every tick, so the stream ignores the state", "[lot]") {
    // Two traders on the same stream.  For 300 ticks one of them sits at its
    // inventory limit with no view of V (no buys left to send); afterwards both see
    // the same market and must act identically.
    RngService rng(11);
    auto p = lot_params();
    p.chart_window = 1;
    LimitOrderTrader a(1, "T", rng.for_consumer("x"), p);
    LimitOrderTrader b(2, "T", rng.for_consumer("x"), p);
    for (Tick k = 1; k <= 300; ++k) {
        a.on_market_data(lot_snap(k, 10100.0));
        b.on_market_data(lot_snap(k, 0.0, 300.0));   // blind and full: buys faded out
    }
    CHECK(a.view_error() == b.view_error());
    for (Tick k = 301; k <= 600; ++k) {
        const auto aa = a.on_market_data(lot_snap(k, 10100.0));
        const auto ab = b.on_market_data(lot_snap(k, 10100.0));
        const auto* sa = first_order(aa);
        const auto* sb = first_order(ab);
        REQUIRE((sa == nullptr) == (sb == nullptr));
        if (sa) { CHECK(sa->price == sb->price); CHECK(sa->qty == sb->qty); CHECK(sa->side == sb->side); }
    }
}

TEST_CASE("LimitOrderTrader: the growing side fades to nothing at q_soft", "[lot]") {
    RngService rng(3);
    auto p = lot_params();
    p.g_chart = 0.0; p.g_noise = 0.0;   // pure fundamentalist: V above → wants to buy
    LimitOrderTrader t(1, "T", rng.for_consumer("lot"), p);
    int buys_flat = 0;
    for (Tick k = 1; k <= 500; ++k) {
        const auto acts = t.on_market_data(lot_snap(k, 10300.0, 0.0));
        const auto* so = first_order(acts);
        if (so && so->side == Side::Buy) ++buys_flat;
    }
    CHECK(buys_flat > 400);
    for (Tick k = 501; k <= 1000; ++k) {
        const auto acts = t.on_market_data(lot_snap(k, 10300.0, p.q_soft));
        CHECK(acts.empty());   // long at the soft limit: no more buys
    }
    // Halfway to the limit the mean buy is about half the flat size.
    double s0 = 0, s1 = 0; int n0 = 0, n1 = 0;
    for (Tick k = 1001; k <= 4000; ++k) {
        const bool half = (k % 2) == 0;
        const auto acts = t.on_market_data(lot_snap(k, 10300.0, half ? 0.5 * p.q_soft : 0.0));
        const auto* so = first_order(acts);
        if (!so) continue;
        if (half) { s1 += so->qty; ++n1; } else { s0 += so->qty; ++n0; }
    }
    REQUIRE(n0 > 0); REQUIRE(n1 > 0);
    CHECK((s1 / n1) / (s0 / n0) == Catch::Approx(0.5).margin(0.12));
}

TEST_CASE("Publisher: share of the touch held by non-makers", "[lot]") {
    OrderBookV2 book;
    auto add = [&](OrderId id, AgentId a, Side s, Price px, Qty q) {
        Order o; o.id = id; o.agent_id = a; o.side = s; o.type = OrderType::Limit;
        o.price = px; o.qty = q; o.seq_no = id; o.ticker = "T";
        REQUIRE(book.add_limit(o).has_value());
    };
    add(1, 10, Side::Buy,  9999, 300);   // maker, best bid
    add(2, 20, Side::Buy,  9999, 100);   // other, best bid
    add(3, 20, Side::Buy,  9990, 500);   // other, deeper: not the touch
    add(4, 10, Side::Sell, 10001, 200);  // maker, best ask
    add(5, 21, Side::Sell, 10001, 200);  // other, best ask
    MarketDataPublisher pub(book);
    pub.set_initial_mid(10000);
    CHECK(std::isnan(pub.publish(1).non_mm_top_frac));   // makers not declared
    pub.set_mm_ids({10});
    CHECK(pub.publish(2).non_mm_top_frac == Catch::Approx(300.0 / 800.0));
}
