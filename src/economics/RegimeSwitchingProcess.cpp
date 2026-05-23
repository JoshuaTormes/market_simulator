#include "RegimeSwitchingProcess.h"
#include <cmath>
#include <cassert>

RegimeSwitchingProcess::RegimeSwitchingProcess(Config cfg, std::mt19937_64 rng)
    : cfg_(cfg), log_s_(std::log(cfg.s0)), regime_(cfg.initial_regime)
    , rng_(std::move(rng))
{
    assert(cfg.s0 > 0.0);
    assert(cfg.initial_regime >= 0 && cfg.initial_regime < kRegimes);
    assert(cfg.nu > 2.0); // need finite variance for unit-variance normalization
    student_t_ = std::student_t_distribution<double>{cfg_.nu};
}

void RegimeSwitchingProcess::transition_regime() {
    double u = uni_(rng_);
    double cumsum = 0.0;
    for (int j = 0; j < kRegimes; ++j) {
        cumsum += cfg_.trans[regime_][j];
        if (u <= cumsum) {
            regime_ = j;
            return;
        }
    }
    regime_ = kRegimes - 1; // fallback (should not reach here if matrix is row-stochastic)
}

void RegimeSwitchingProcess::step(Tick /*now*/, double dt) {
    transition_regime();
    const Regime& r = cfg_.regimes[regime_];
    // Student-t(ν) innovation normalized to unit variance: var[t(ν)] = ν/(ν-2).
    // This preserves calibrated σ values while giving power-law tails with exponent ν.
    double z = student_t_(rng_) / std::sqrt(cfg_.nu / (cfg_.nu - 2.0));
    log_s_ += (r.mu - 0.5 * r.sigma * r.sigma) * dt
             + r.sigma * std::sqrt(dt) * z;
}

double RegimeSwitchingProcess::current_value() const {
    return std::exp(log_s_);
}
