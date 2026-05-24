// TWAP with Pareto-distributed child sizes: heavy-tailed trade flow → fat-tailed returns.
#include "InstitutionalExecutor.h"
#include <algorithm>
#include <cmath>

InstitutionalExecutor::InstitutionalExecutor(AgentId id, const std::string& ticker,
                                             std::mt19937_64 rng, Params p,
                                             RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , remaining_(p.parent_qty)
{}

std::vector<Action> InstitutionalExecutor::on_market_data(const AgentSnapshot& snap) {
    if (remaining_ <= 0) return {};

    Tick now = snap.base.tick;
    if (!started_) {
        next_tick_ = now;
        started_   = true;
    }
    if (now < next_tick_) return {};

    int slices_left = p_.total_slices - slices_done_;
    if (slices_left <= 0) return {};

    Qty base_qty  = remaining_ / slices_left;
    base_qty      = std::max(base_qty, Qty{1});

    Qty child_qty = base_qty;
    if (p_.pareto_alpha > 0.0) {
        // Pareto(1, alpha) draw — unbiased multiplier: E = 1 when alpha > 1.
        // u clamped away from 1.0 to avoid infinite draw.
        std::uniform_real_distribution<double> u_dist(0.0, 0.9999);
        double u = u_dist(rng_);
        double multiplier = ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
                          * std::pow(1.0 - u, -1.0 / p_.pareto_alpha);
        child_qty = static_cast<Qty>(std::max(1.0, std::round(
                        std::min(static_cast<double>(p_.max_child_qty),
                                 static_cast<double>(base_qty) * multiplier))));
    }
    child_qty = std::min(child_qty, remaining_);

    remaining_    -= child_qty;
    ++slices_done_;
    next_tick_     = now + static_cast<Tick>(p_.ticks_between);

    return { submit(p_.side, OrderType::Market, 0, child_qty) };
}
