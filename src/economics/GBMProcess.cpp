#include "GBMProcess.h"
#include <cmath>
#include <cassert>

GBMProcess::GBMProcess(Config cfg, std::mt19937_64 rng)
    : log_s_(std::log(cfg.s0)), mu_(cfg.mu), sigma_(cfg.sigma), rng_(std::move(rng))
{
    assert(cfg.s0 > 0.0);
    assert(cfg.sigma >= 0.0);
}

void GBMProcess::step(Tick /*now*/, double dt) {
    double z = norm_(rng_);
    log_s_ += (mu_ - 0.5 * sigma_ * sigma_) * dt
             + sigma_ * std::sqrt(dt) * z;
}

double GBMProcess::current_value() const {
    return std::exp(log_s_);
}
