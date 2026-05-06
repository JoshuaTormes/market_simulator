#include "PoissonNewsProcess.h"
#include <cmath>
#include <cassert>

PoissonNewsProcess::PoissonNewsProcess(Config cfg, std::mt19937_64 rng, EventBus* bus)
    : cfg_(cfg), bus_(bus), rng_(std::move(rng))
    , poisson_(cfg.lambda)
    , chi2_(static_cast<double>(cfg.student_t_df))
{
    assert(cfg.lambda > 0.0);
    assert(cfg.student_t_df >= 1);
    assert(cfg.magnitude_scale > 0.0);
}

double PoissonNewsProcess::sample_student_t() {
    // T = Z / sqrt(χ²_ν / ν), Z ~ N(0,1), χ²_ν ~ Chi²(ν)
    double z = norm_(rng_);
    double v = chi2_(rng_);
    return z / std::sqrt(v / static_cast<double>(cfg_.student_t_df));
}

std::vector<NewsEvent> PoissonNewsProcess::step(Tick now) {
    int n = poisson_(rng_);
    std::vector<NewsEvent> events;
    events.reserve(n);

    for (int i = 0; i < n; ++i) {
        double t = sample_student_t();
        double sign = side_(rng_) ? 1.0 : -1.0;
        double impact = sign * cfg_.magnitude_scale * std::abs(t);

        NewsEvent ev;
        ev.announce_tick       = now;
        ev.ticker              = cfg_.ticker;
        ev.impact_log_return   = impact;
        ev.duration_ticks      = cfg_.duration_ticks;
        ev.dispersion_sigma    = cfg_.dispersion_sigma;

        events.push_back(ev);
        if (bus_) bus_->publish(ev);
    }
    return events;
}
