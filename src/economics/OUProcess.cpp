#include "OUProcess.h"
#include <cmath>
#include <cassert>

OUProcess::OUProcess(Config cfg, std::mt19937_64 rng)
    : s_(cfg.s0), theta_(cfg.theta), kappa_(cfg.kappa), sigma_(cfg.sigma)
    , rng_(std::move(rng))
{
    assert(cfg.kappa > 0.0);
    assert(cfg.sigma >= 0.0);
}

void OUProcess::step(Tick /*now*/, double dt) {
    double exp_k   = std::exp(-kappa_ * dt);
    double cond_sd = sigma_ * std::sqrt((1.0 - std::exp(-2.0 * kappa_ * dt)) / (2.0 * kappa_));
    double z       = norm_(rng_);
    s_ = theta_ + (s_ - theta_) * exp_k + cond_sd * z;
}
