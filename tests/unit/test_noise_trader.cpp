#include <catch_amalgamated.hpp>
#include "agents/NoiseTrader.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot flat_snap(double own_inv = 0.0) {
    AgentSnapshot as;
    as.base.mid_price  = 10000;
    as.base.spread     = 2;
    as.perceived_mid   = 10000;
    as.own_inventory   = own_inv;
    return as;
}

// Fraction of buys over N acting ticks at a fixed inventory.
static double buy_fraction(uint64_t seed, double own_inv, double q_scale, int N) {
    RngService rng(seed);
    NoiseTrader::Params p;
    p.p_act = 1.0; p.size_mu = 1.0; p.size_sigma = 0.0;
    p.max_size = 1000; p.q_scale = q_scale;

    NoiseTrader nt(1, "T", rng.for_consumer("nt"), p);
    AgentSnapshot snap = flat_snap(own_inv);
    int buys = 0;
    for (int i = 0; i < N; ++i) {
        auto acts = nt.on_market_data(snap);
        if (!acts.empty() && std::get<SubmitOrder>(acts[0]).side == Side::Buy) ++buys;
    }
    return static_cast<double>(buys) / N;
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

TEST_CASE("NoiseTrader: the side tilts against the inventory", "[noise][inventory]") {
    // P(buy) = 0.5 - 0.5*tanh(q/q_scale).  At q = q_scale the coin should
    // already be about 24/76 against adding to the position.
    const int N = 40'000;
    const double q_scale = 500.0;

    CHECK(buy_fraction(11,  0.0,     q_scale, N) == Catch::Approx(0.5).epsilon(0.03));
    CHECK(buy_fraction(11,  q_scale, q_scale, N)
          == Catch::Approx(0.5 - 0.5 * std::tanh(1.0)).epsilon(0.05));
    CHECK(buy_fraction(11, -q_scale, q_scale, N)
          == Catch::Approx(0.5 + 0.5 * std::tanh(1.0)).epsilon(0.05));
}

TEST_CASE("NoiseTrader: the tilt saturates far from flat", "[noise][inventory]") {
    // Three scale lengths out, the trader is effectively one-sided: this is
    // what keeps the cohort from random-walking into its position limit.
    const int N = 20'000;
    CHECK(buy_fraction(23,  1500.0, 500.0, N) < 0.03);
    CHECK(buy_fraction(23, -1500.0, 500.0, N) > 0.97);
}

TEST_CASE("NoiseTrader: the RNG stream does not depend on the inventory",
          "[noise][determinism]") {
    // The side draw is taken unconditionally so that two runs differing only in
    // position history stay aligned in the stream — sizes must match tick for
    // tick even when the sides diverge.
    RngService rng1(5), rng2(5);
    NoiseTrader::Params p;
    p.p_act = 0.5; p.size_mu = 1.0; p.size_sigma = 0.5; p.max_size = 500;

    NoiseTrader a(1, "T", rng1.for_consumer("nt"), p);
    NoiseTrader b(1, "T", rng2.for_consumer("nt"), p);
    AgentSnapshot flat = flat_snap(0.0);
    AgentSnapshot lng  = flat_snap(900.0);

    for (int i = 0; i < 300; ++i) {
        auto aa = a.on_market_data(flat);
        auto bb = b.on_market_data(lng);
        REQUIRE(aa.size() == bb.size());
        if (!aa.empty())
            CHECK(std::get<SubmitOrder>(aa[0]).qty == std::get<SubmitOrder>(bb[0]).qty);
    }
}
