#include <catch_amalgamated.hpp>
#include "marketdata/AgentSnapshot.h"
#include "marketdata/MarketSnapshot.h"
#include "core/RngService.h"
#include <cmath>
#include <numeric>
#include <vector>

static MarketSnapshot make_snap(Price mid, Price last, Tick tick = 1) {
    MarketSnapshot s;
    s.tick              = tick;
    s.mid_price         = mid;
    s.last_trade_price  = last;
    s.spread            = 2;
    return s;
}

// ── Zero noise ────────────────────────────────────────────────────────────

TEST_CASE("agent_snapshot: zero noise → perceived == real", "[agent_snapshot]") {
    MarketSnapshot snap = make_snap(100, 99);
    InformationProfile info;
    info.sigma_price_noise = 0.0;

    RngService rng(42);
    auto eng = rng.for_consumer("agent_1");

    auto as = AgentSnapshot::derive_from(snap, info, eng);
    CHECK(as.perceived_mid  == 100);
    CHECK(as.perceived_last == 99);
}

// ── Noise distribution ────────────────────────────────────────────────────

TEST_CASE("agent_snapshot: noise samples are approximately N(0, sigma)", "[agent_snapshot]") {
    const Price real_mid = 1000;
    const double sigma   = 10.0;
    const int    N       = 5000;

    MarketSnapshot snap = make_snap(real_mid, real_mid);
    InformationProfile info;
    info.sigma_price_noise = sigma;

    RngService rng(12345);

    std::vector<double> samples;
    samples.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto eng = rng.for_consumer("agent_" + std::to_string(i));
        auto as  = AgentSnapshot::derive_from(snap, info, eng);
        samples.push_back(static_cast<double>(as.perceived_mid - real_mid));
    }

    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / N;
    double sq_sum = 0.0;
    for (double x : samples) sq_sum += (x - mean) * (x - mean);
    double std_dev = std::sqrt(sq_sum / N);

    // Mean should be close to 0 (within 3·sigma/sqrt(N) ≈ 0.42)
    CHECK(std::abs(mean) < 1.0);
    // Std dev should be close to sigma (within 5% relative)
    CHECK(std_dev == Catch::Approx(sigma).epsilon(0.05));
}

// ── Reproducibility ───────────────────────────────────────────────────────

TEST_CASE("agent_snapshot: same seed → same perceived price", "[agent_snapshot]") {
    MarketSnapshot snap = make_snap(500, 499);
    InformationProfile info;
    info.sigma_price_noise = 5.0;

    RngService rng_a(99), rng_b(99);
    auto eng_a = rng_a.for_consumer("trader_7");
    auto eng_b = rng_b.for_consumer("trader_7");

    auto as_a = AgentSnapshot::derive_from(snap, info, eng_a);
    auto as_b = AgentSnapshot::derive_from(snap, info, eng_b);

    CHECK(as_a.perceived_mid  == as_b.perceived_mid);
    CHECK(as_a.perceived_last == as_b.perceived_last);
}

// ── Different agents get different noise ──────────────────────────────────

TEST_CASE("agent_snapshot: different agent consumers get different perceived prices", "[agent_snapshot]") {
    MarketSnapshot snap = make_snap(200, 200);
    InformationProfile info;
    info.sigma_price_noise = 20.0;

    RngService rng(777);
    auto eng_a = rng.for_consumer("agent_1");
    auto eng_b = rng.for_consumer("agent_2");

    auto as_a = AgentSnapshot::derive_from(snap, info, eng_a);
    auto as_b = AgentSnapshot::derive_from(snap, info, eng_b);

    // Very unlikely (but possible) that two different consumers produce the same noise
    // This is probabilistic — with sigma=20 on int64 rounding, collision probability is very low
    // If this flaps, increase sigma or pick a seed that guarantees divergence
    CHECK(as_a.perceived_mid != as_b.perceived_mid);
}

// ── Base snapshot preserved ───────────────────────────────────────────────

TEST_CASE("agent_snapshot: base snapshot fields preserved unmodified", "[agent_snapshot]") {
    MarketSnapshot snap = make_snap(300, 295);
    snap.spread = 4;
    snap.momentum = 0.02;

    InformationProfile info;
    info.sigma_price_noise = 3.0;

    RngService rng(1);
    auto eng = rng.for_consumer("x");
    auto as  = AgentSnapshot::derive_from(snap, info, eng);

    CHECK(as.base.spread   == 4);
    CHECK(as.base.momentum == Catch::Approx(0.02));
    CHECK(as.base.tick     == 1);
}
