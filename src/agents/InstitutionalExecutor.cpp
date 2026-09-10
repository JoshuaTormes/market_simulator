// TWAP with Pareto-distributed parent and child sizes: heavy-tailed trade flow
// → fat-tailed returns and persistent trade-sign autocorrelation.
#include "InstitutionalExecutor.h"
#include <algorithm>
#include <cmath>

InstitutionalExecutor::InstitutionalExecutor(AgentId id, const std::string& ticker,
                                             std::mt19937_64 rng, Params p,
                                             RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , arrival_(std::clamp(p.arrival_lambda, 0.0, 1.0))
    , side_coin_(0.5)
    , u_(0.0, 0.9999)
{}

Qty InstitutionalExecutor::draw_parent_qty() {
    const double lo = static_cast<double>(std::max<Qty>(p_.parent_min, 1));
    const double hi = static_cast<double>(std::max<Qty>(p_.parent_max, p_.parent_min));
    if (p_.parent_alpha <= 0.0) return static_cast<Qty>(lo);
    // Inverse-CDF of Pareto(lo, alpha), then truncate at hi.  Truncating rather
    // than rejecting keeps the RNG stream one draw per parent, which the
    // determinism tests depend on.
    const double q = lo * std::pow(1.0 - u_(rng_), -1.0 / p_.parent_alpha);
    return static_cast<Qty>(std::llround(std::min(q, hi)));
}

std::vector<Action> InstitutionalExecutor::on_market_data(const AgentSnapshot& snap) {
    const Tick now = snap.base.tick;

    // Idle: maybe a new parent order arrives.  The Bernoulli draw happens every
    // idle tick so the arrival process is memoryless in tick time.
    if (remaining_ <= 0) {
        if (!arrival_(rng_)) return {};
        side_        = side_coin_(rng_) ? Side::Buy : Side::Sell;
        remaining_   = draw_parent_qty();
        slices_done_ = 0;
        next_tick_   = now;
        ++parents_started_;
    }

    if (now < next_tick_) return {};

    int slices_left = p_.total_slices - slices_done_;
    if (slices_left <= 0) {           // schedule exhausted: drop the residual
        remaining_ = 0;
        return {};
    }

    Qty base_qty = std::max<Qty>(remaining_ / slices_left, 1);

    Qty child_qty = base_qty;
    if (p_.pareto_alpha > 0.0) {
        // Pareto(1, alpha) draw — unbiased multiplier: E = 1 when alpha > 1.
        double multiplier = ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
                          * std::pow(1.0 - u_(rng_), -1.0 / p_.pareto_alpha);
        child_qty = static_cast<Qty>(std::max(1.0, std::round(
                        std::min(static_cast<double>(p_.max_child_qty),
                                 static_cast<double>(base_qty) * multiplier))));
    }
    child_qty = std::min(child_qty, remaining_);

    remaining_ -= child_qty;
    ++slices_done_;
    next_tick_  = now + static_cast<Tick>(p_.ticks_between);

    return { submit(side_, OrderType::Market, 0, child_qty) };
}
