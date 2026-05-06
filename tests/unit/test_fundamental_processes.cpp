#include <catch_amalgamated.hpp>
#include "economics/GBMProcess.h"
#include "economics/JumpDiffusionProcess.h"
#include "economics/OUProcess.h"
#include "economics/RegimeSwitchingProcess.h"
#include "core/RngService.h"
#include <cmath>
#include <vector>
#include <numeric>

static std::mt19937_64 make_rng(const std::string& tag, uint64_t seed = 42) {
    RngService rng(seed);
    return rng.for_consumer(tag);
}

// ── GBM ───────────────────────────────────────────────────────────────────

TEST_CASE("GBM: initial value is s0", "[gbm]") {
    GBMProcess::Config cfg;
    cfg.s0 = 200.0; cfg.mu = 0.0; cfg.sigma = 0.01;
    GBMProcess proc(cfg, make_rng("gbm_init"));
    CHECK(proc.current_value() == Catch::Approx(200.0).epsilon(1e-12));
}

TEST_CASE("GBM: value always positive", "[gbm]") {
    GBMProcess::Config cfg{ .s0=100.0, .mu=0.001, .sigma=0.05 };
    GBMProcess proc(cfg, make_rng("gbm_pos"));
    for (int i = 0; i < 10000; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        CHECK(proc.current_value() > 0.0);
    }
}

TEST_CASE("GBM: mean(log_returns) ≈ (μ − σ²/2)·dt, var ≈ σ²·dt", "[gbm][statistics]") {
    const int N = 200'000;
    const double mu = 0.0002, sigma = 0.01, dt = 1.0;

    GBMProcess::Config cfg{ .s0=100.0, .mu=mu, .sigma=sigma };
    GBMProcess proc(cfg, make_rng("gbm_stats"));

    double prev = proc.current_value();
    std::vector<double> returns;
    returns.reserve(N);

    for (int i = 0; i < N; ++i) {
        proc.step(static_cast<Tick>(i), dt);
        double curr = proc.current_value();
        returns.push_back(std::log(curr / prev));
        prev = curr;
    }

    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / N;
    double expected_mean = (mu - 0.5 * sigma * sigma) * dt;

    double var = 0.0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= N;
    double expected_var = sigma * sigma * dt;

    // Allow 5-sigma Monte Carlo tolerance
    double se_mean = sigma / std::sqrt(static_cast<double>(N)); // ≈ 2.24e-5
    CHECK(std::abs(mean - expected_mean) < 5.0 * se_mean);

    // Variance within 5% relative
    CHECK(var == Catch::Approx(expected_var).epsilon(0.05));
}

TEST_CASE("GBM: deterministic with same seed", "[gbm][determinism]") {
    GBMProcess::Config cfg{ .s0=100.0, .mu=0.001, .sigma=0.02 };
    GBMProcess a(cfg, make_rng("gbm_det", 7));
    GBMProcess b(cfg, make_rng("gbm_det", 7));

    for (int i = 0; i < 100; ++i) {
        a.step(static_cast<Tick>(i), 1.0);
        b.step(static_cast<Tick>(i), 1.0);
    }
    CHECK(a.current_value() == Catch::Approx(b.current_value()).epsilon(1e-12));
}

// ── Jump-Diffusion ────────────────────────────────────────────────────────

TEST_CASE("JumpDiffusion: value always positive", "[jd]") {
    JumpDiffusionProcess::Config cfg{
        .s0=100.0, .mu=0.0, .sigma=0.01,
        .lambda_jump=0.1, .mu_jump=0.0, .sigma_jump=0.03
    };
    JumpDiffusionProcess proc(cfg, make_rng("jd_pos"));
    for (int i = 0; i < 5000; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        CHECK(proc.current_value() > 0.0);
    }
}

TEST_CASE("JumpDiffusion: kurtosis > 3 (fat tails from jumps)", "[jd][statistics]") {
    const int N = 100'000;
    JumpDiffusionProcess::Config cfg{
        .s0=100.0, .mu=0.0, .sigma=0.005,
        .lambda_jump=0.1, .mu_jump=0.0, .sigma_jump=0.05
    };
    JumpDiffusionProcess proc(cfg, make_rng("jd_kurt"));

    double prev = proc.current_value();
    std::vector<double> returns;
    returns.reserve(N);

    for (int i = 0; i < N; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        double curr = proc.current_value();
        returns.push_back(std::log(curr / prev));
        prev = curr;
    }

    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / N;
    double m2 = 0.0, m4 = 0.0;
    for (double r : returns) {
        double d = r - mean;
        m2 += d * d;
        m4 += d * d * d * d;
    }
    m2 /= N; m4 /= N;
    double kurtosis = (m2 > 0) ? m4 / (m2 * m2) : 0.0;
    CHECK(kurtosis > 3.0); // fat tails: excess kurtosis > 0
}

// ── OU Process ────────────────────────────────────────────────────────────

TEST_CASE("OU: converges to theta in long run", "[ou]") {
    OUProcess::Config cfg{ .s0=200.0, .theta=100.0, .kappa=0.5, .sigma=1.0 };
    OUProcess proc(cfg, make_rng("ou_conv"));

    // After many steps, value should be near theta
    for (int i = 0; i < 10000; ++i)
        proc.step(static_cast<Tick>(i), 1.0);

    CHECK(proc.current_value() == Catch::Approx(cfg.theta).margin(10.0));
}

TEST_CASE("OU: long-run variance ≈ σ²/(2κ)", "[ou][statistics]") {
    const int N = 200'000;
    OUProcess::Config cfg{ .s0=100.0, .theta=100.0, .kappa=0.05, .sigma=1.0 };
    OUProcess proc(cfg, make_rng("ou_var"));

    // Burn-in
    for (int i = 0; i < 10000; ++i)
        proc.step(static_cast<Tick>(i), 1.0);

    // Sample
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i < N; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        double v = proc.current_value();
        sum  += v;
        sum2 += v * v;
    }
    double mean = sum / N;
    double var  = sum2 / N - mean * mean;
    double expected_var = (cfg.sigma * cfg.sigma) / (2.0 * cfg.kappa);

    CHECK(mean == Catch::Approx(cfg.theta).margin(0.5));
    CHECK(var  == Catch::Approx(expected_var).epsilon(0.05)); // within 5%
}

TEST_CASE("OU: drift direction points toward theta", "[ou]") {
    OUProcess::Config cfg{ .s0=50.0, .theta=100.0, .kappa=0.2, .sigma=0.0 };
    OUProcess proc(cfg, make_rng("ou_drift"));

    // With σ=0, value monotonically approaches theta (non-decreasing from below).
    // Limit to 60 steps — beyond that the remaining gap falls below float64 precision
    // (exp(-0.2)^60 * 50 ≈ 2e-5, still measurable).
    double prev = proc.current_value();
    for (int i = 0; i < 60; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        double curr = proc.current_value();
        CHECK(curr >= prev); // non-decreasing (strictly increasing until convergence)
        prev = curr;
    }
    // After 60 steps: value should be very close to theta
    CHECK(proc.current_value() == Catch::Approx(cfg.theta).margin(0.01));
}

// ── Regime Switching ──────────────────────────────────────────────────────

TEST_CASE("RegimeSwitching: initial regime is set correctly", "[regime]") {
    RegimeSwitchingProcess::Config cfg;
    cfg.initial_regime = 1;
    RegimeSwitchingProcess proc(cfg, make_rng("rs_init"));
    CHECK(proc.current_regime() == 1);
}

TEST_CASE("RegimeSwitching: value always positive", "[regime]") {
    RegimeSwitchingProcess::Config cfg;
    RegimeSwitchingProcess proc(cfg, make_rng("rs_pos"));
    for (int i = 0; i < 5000; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        CHECK(proc.current_value() > 0.0);
    }
}

TEST_CASE("RegimeSwitching: all regimes eventually visited", "[regime]") {
    RegimeSwitchingProcess::Config cfg;
    RegimeSwitchingProcess proc(cfg, make_rng("rs_regimes"));

    bool seen[RegimeSwitchingProcess::kRegimes] = {};
    for (int i = 0; i < 10000; ++i) {
        proc.step(static_cast<Tick>(i), 1.0);
        seen[proc.current_regime()] = true;
    }
    for (int r = 0; r < RegimeSwitchingProcess::kRegimes; ++r) {
        CHECK(seen[r]); // all regimes visited in 10k steps
    }
}
