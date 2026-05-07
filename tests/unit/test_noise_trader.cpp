#include <catch_amalgamated.hpp>
#include "agents/NoiseTrader.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot flat_snap() {
    AgentSnapshot as;
    as.base.mid_price  = 10000;
    as.base.spread     = 2;
    as.perceived_mid   = 10000;
    return as;
}

TEST_CASE("NoiseTrader: acts with correct probability", "[noise]") {
    const int N = 50'000;
    const double p_act = 0.20;

    RngService rng(42);
    NoiseTrader::Params p;
    p.p_act      = p_act;
    p.size_mu    = 1.0;
    p.size_sigma = 0.0;  // deterministic size = exp(1) ≈ 2.7 → rounds to 3
    p.max_size   = 1000;

    NoiseTrader nt(1, "T", rng.for_consumer("nt"), p);
    int active = 0;
    AgentSnapshot snap = flat_snap();
    for (int i = 0; i < N; ++i)
        active += (int)!nt.on_market_data(snap).empty();

    double empirical = static_cast<double>(active) / N;
    double se = std::sqrt(p_act * (1 - p_act) / N);
    CHECK(std::abs(empirical - p_act) < 5.0 * se);
}

TEST_CASE("NoiseTrader: order is Market type with positive qty", "[noise]") {
    RngService rng(7);
    NoiseTrader::Params p;
    p.p_act = 1.0;  // always act
    p.size_mu = 1.5; p.size_sigma = 0.3; p.max_size = 500;

    NoiseTrader nt(1, "T", rng.for_consumer("nt"), p);
    AgentSnapshot snap = flat_snap();

    for (int i = 0; i < 100; ++i) {
        auto acts = nt.on_market_data(snap);
        REQUIRE(acts.size() == 1);
        auto* so = std::get_if<SubmitOrder>(&acts[0]);
        REQUIRE(so != nullptr);
        CHECK(so->type == OrderType::Market);
        CHECK(so->qty > 0);
    }
}

TEST_CASE("NoiseTrader: roughly equal buy/sell split", "[noise]") {
    const int N = 20'000;
    RngService rng(42);
    NoiseTrader::Params p;
    p.p_act = 1.0; p.size_mu = 1.0; p.size_sigma = 0.0; p.max_size = 1000;

    NoiseTrader nt(1, "T", rng.for_consumer("nt"), p);
    int buys = 0, sells = 0;
    AgentSnapshot snap = flat_snap();
    for (int i = 0; i < N; ++i) {
        auto acts = nt.on_market_data(snap);
        if (!acts.empty()) {
            if (std::get<SubmitOrder>(acts[0]).side == Side::Buy) ++buys;
            else ++sells;
        }
    }
    // Expect ~50/50 ± 3σ
    double se = std::sqrt(0.5 * 0.5 / N);
    double frac = static_cast<double>(buys) / N;
    CHECK(std::abs(frac - 0.5) < 5.0 * se);
}

TEST_CASE("NoiseTrader: same seed → same sequence", "[noise][determinism]") {
    RngService rng1(99), rng2(99);
    NoiseTrader::Params p;
    p.p_act = 0.5; p.size_mu = 1.0; p.size_sigma = 0.3; p.max_size = 100;

    NoiseTrader a(1, "T", rng1.for_consumer("nt"), p);
    NoiseTrader b(1, "T", rng2.for_consumer("nt"), p);
    AgentSnapshot snap = flat_snap();

    for (int i = 0; i < 200; ++i) {
        auto aa = a.on_market_data(snap);
        auto ab = b.on_market_data(snap);
        REQUIRE(aa.size() == ab.size());
        if (!aa.empty()) {
            auto* sa = std::get_if<SubmitOrder>(&aa[0]);
            auto* sb = std::get_if<SubmitOrder>(&ab[0]);
            REQUIRE(sa != nullptr);
            REQUIRE(sb != nullptr);
            CHECK(sa->side == sb->side);
            CHECK(sa->qty  == sb->qty);
        }
    }
}
