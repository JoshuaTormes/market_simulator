#include "JumpDiffusionProcess.h"
#include <cmath>
#include <cassert>

JumpDiffusionProcess::JumpDiffusionProcess(Config cfg, std::mt19937_64 rng)
    : log_s_(std::log(cfg.s0))
    , mu_(cfg.mu), sigma_(cfg.sigma)
    , lambda_j_(cfg.lambda_jump), mu_j_(cfg.mu_jump), sigma_j_(cfg.sigma_jump)
    , rng_(std::move(rng))
    , poisson_(cfg.lambda_jump)
{
    assert(cfg.s0 > 0.0);
    assert(cfg.sigma >= 0.0);
    assert(cfg.lambda_jump >= 0.0);

    // Compensated drift: remove the average jump contribution so E[dS/S] = μ dt
    double m_j = std::exp(mu_j_ + 0.5 * sigma_j_ * sigma_j_) - 1.0;
    mu_eff_ = mu_ - lambda_j_ * m_j;
}

void JumpDiffusionProcess::step(Tick /*now*/, double dt) {
    // Diffusion part
    double z = norm_(rng_);
    log_s_ += (mu_eff_ - 0.5 * sigma_ * sigma_) * dt
             + sigma_ * std::sqrt(dt) * z;

    // Jump part: number of jumps this step ~ Poisson(λ_j · dt)
    // Approximate: use stored poisson_ scaled by dt
    // For dt = 1: use stored distribution. For dt ≠ 1: re-sample.
    std::poisson_distribution<int> pois(lambda_j_ * dt);
    int n_jumps = pois(rng_);
    for (int i = 0; i < n_jumps; ++i) {
        double log_jump = mu_j_ + sigma_j_ * norm_(rng_);
        log_s_ += log_jump;
    }
}

double JumpDiffusionProcess::current_value() const {
    return std::exp(log_s_);
}
