#include <catch_amalgamated.hpp>
#include "economics/PoissonNewsProcess.h"
#include "core/RngService.h"
#include "core/EventBus.h"
#include <cmath>
#include <numeric>
#include <vector>

static std::mt19937_64 make_rng(const std::string& tag, uint64_t seed = 42) {
    RngService rng(seed);
    return rng.for_consumer(tag);
}

// ── Poisson arrival count ─────────────────────────────────────────────────

TEST_CASE("news: no events when lambda = 0", "[news]") {
    PoissonNewsProcess::Config cfg;
    cfg.lambda = 1e-300; // effectively zero
    PoissonNewsProcess proc(cfg, make_rng("news_zero"));

    int total = 0;
    for (int t = 0; t < 1000; ++t)
        total += static_cast<int>(proc.step(static_cast<Tick>(t)).size());
    CHECK(total == 0);
}

TEST_CASE("news: mean event count per tick ≈ lambda", "[news][statistics]") {
    const int N     = 100'000;
    const double lam = 0.05;
    PoissonNewsProcess::Config cfg;
    cfg.lambda = lam;
    PoissonNewsProcess proc(cfg, make_rng("news_mean"));

    int total = 0;
    for (int t = 0; t < N; ++t)
        total += static_cast<int>(proc.step(static_cast<Tick>(t)).size());

    double mean = static_cast<double>(total) / N;
    // Poisson: std = sqrt(λ) / sqrt(N) for mean estimator ≈ sqrt(0.05/100000) ≈ 0.000707
    double se = std::sqrt(lam / N);
    CHECK(std::abs(mean - lam) < 5.0 * se);
}

TEST_CASE("news: variance of count ≈ lambda (Poisson equidispersion)", "[news][statistics]") {
    const int N = 20'000;
    const double lam = 0.1;
    PoissonNewsProcess::Config cfg;
    cfg.lambda = lam;
    PoissonNewsProcess proc(cfg, make_rng("news_var"));

    std::vector<int> counts(N);
    for (int t = 0; t < N; ++t)
        counts[t] = static_cast<int>(proc.step(static_cast<Tick>(t)).size());

    double mean = static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0)) / N;
    double var  = 0.0;
    for (int c : counts) var += (c - mean) * (c - mean);
    var /= N;

    // For Poisson, variance == mean
    CHECK(var == Catch::Approx(lam).epsilon(0.10));
}

// ── Impact fat tails ──────────────────────────────────────────────────────

TEST_CASE("news: impact magnitudes have fat tails (kurtosis > 3)", "[news][statistics]") {
    const int N = 200'000;
    PoissonNewsProcess::Config cfg;
    cfg.lambda = 1.0; // one event per tick for fast sampling
    cfg.magnitude_scale = 1.0;
    PoissonNewsProcess proc(cfg, make_rng("news_kurt"));

    std::vector<double> impacts;
    impacts.reserve(N);
    for (int t = 0; t < N; ++t) {
        auto evts = proc.step(static_cast<Tick>(t));
        for (auto& ev : evts)
            impacts.push_back(ev.impact_log_return);
    }

    double mean = std::accumulate(impacts.begin(), impacts.end(), 0.0) / impacts.size();
    double m2 = 0.0, m4 = 0.0;
    for (double x : impacts) {
        double d = x - mean;
        m2 += d * d;
        m4 += d * d * d * d;
    }
    m2 /= impacts.size(); m4 /= impacts.size();
    double kurtosis = (m2 > 0) ? m4 / (m2 * m2) : 0.0;

    CHECK(kurtosis > 3.0); // fat tails: excess kurtosis > 0
}

// ── EventBus integration ──────────────────────────────────────────────────

TEST_CASE("news: events published to EventBus", "[news][eventbus]") {
    EventBus bus;
    std::vector<NewsEvent> received;
    bus.subscribe<NewsEvent>([&](const NewsEvent& ev) {
        received.push_back(ev);
    });

    PoissonNewsProcess::Config cfg;
    cfg.lambda = 2.0; // high lambda → many events
    PoissonNewsProcess proc(cfg, make_rng("news_bus"), &bus);

    int direct = 0;
    for (int t = 0; t < 100; ++t)
        direct += static_cast<int>(proc.step(static_cast<Tick>(t)).size());

    // Everything returned from step() was also published to bus
    CHECK(static_cast<int>(received.size()) == direct);
}

// ── NewsEvent fields ──────────────────────────────────────────────────────

TEST_CASE("news: event fields are populated", "[news]") {
    PoissonNewsProcess::Config cfg;
    cfg.lambda           = 100.0;
    cfg.duration_ticks   = 50.0;
    cfg.dispersion_sigma = 0.25;
    cfg.ticker           = "TEST";
    PoissonNewsProcess proc(cfg, make_rng("news_fields"));

    std::vector<NewsEvent> evts;
    for (int t = 0; t < 10; ++t) {
        auto v = proc.step(static_cast<Tick>(t));
        evts.insert(evts.end(), v.begin(), v.end());
        if (!evts.empty()) break;
    }
    REQUIRE(!evts.empty());
    CHECK(evts[0].ticker           == "TEST");
    CHECK(evts[0].duration_ticks   == Catch::Approx(50.0));
    CHECK(evts[0].dispersion_sigma == Catch::Approx(0.25));
    CHECK(evts[0].impact_log_return != 0.0);
}

// ── Determinism ───────────────────────────────────────────────────────────

TEST_CASE("news: same seed → same event sequence", "[news][determinism]") {
    PoissonNewsProcess::Config cfg;
    cfg.lambda = 0.5;

    PoissonNewsProcess a(cfg, make_rng("news_det", 99));
    PoissonNewsProcess b(cfg, make_rng("news_det", 99));

    for (int t = 0; t < 200; ++t) {
        auto ea = a.step(static_cast<Tick>(t));
        auto eb = b.step(static_cast<Tick>(t));
        REQUIRE(ea.size() == eb.size());
        for (size_t i = 0; i < ea.size(); ++i)
            CHECK(ea[i].impact_log_return == Catch::Approx(eb[i].impact_log_return).epsilon(1e-12));
    }
}
