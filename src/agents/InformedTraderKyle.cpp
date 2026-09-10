// S    = V · exp(ε),  ε ~ N(0, signal_noise_log)
// gap  = S − mid                                    (price ticks)
// trade if |gap| > spread/2 + margin_ticks, size = min(cap, lambda_inv·|gap|·pareto)
// else unwind min(unwind_qty, |q|) toward flat
#include "InformedTraderKyle.h"
#include <algorithm>
#include <cmath>

InformedTraderKyle::InformedTraderKyle(AgentId id, const std::string& ticker,
                                       std::mt19937_64 rng, Params p,
                                       RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , log_noise_(0.0, p.signal_noise_log)
{}

std::vector<Action> InformedTraderKyle::on_market_data(const AgentSnapshot& snap) {
    if (!snap.has_fundamental) return {};

    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    // Private value: a proportional error on V, so the signal quality does not
    // depend on the price level.
    const double S   = snap.fundamental_value * std::exp(log_noise_(rng_));
    const double gap = S - mid;

    // Crossing the spread costs half of it; a gap smaller than that is not an
    // opportunity, it is a fee.
    const double half_spread = 0.5 * static_cast<double>(std::max<Price>(snap.base.spread, 0));
    const double threshold   = half_spread + p_.margin_ticks;

    const double q = snap.own_inventory;

    if (std::abs(gap) <= threshold) {
        // Nothing to trade on.  Give back inventory so the next signal finds
        // room under the position limit.
        if (std::abs(q) < 1.0) return {};
        const Side side = (q > 0.0) ? Side::Sell : Side::Buy;
        const Qty  qty  = static_cast<Qty>(
            std::min(static_cast<double>(p_.unwind_qty), std::abs(q)));
        if (qty <= 0) return {};
        return { submit(side, OrderType::Market, 0, qty) };
    }

    double size = p_.lambda_inv * std::abs(gap);
    if (p_.pareto_alpha > 1.0) {
        // Pareto multiplier with unit mean: keeps the Kyle direction and the
        // average size, adds the heavy tail real informed flow shows.
        std::uniform_real_distribution<double> u_dist(0.0, 0.9999);
        const double u = u_dist(rng_);
        size *= ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
              * std::pow(1.0 - u, -1.0 / p_.pareto_alpha);
    }

    const Side side = (gap > 0.0) ? Side::Buy : Side::Sell;

    // Fade the side that grows the position as it approaches the risk limit.
    // Without this the trader keeps sending orders the risk gate rejects while
    // a long-lived gap persists, and spends the rest of the run pinned at the
    // limit with no capacity left for the next signal.
    const double q_max = static_cast<double>(rl_.max_position);
    const bool   grows = (q == 0.0) || ((gap > 0.0) == (q > 0.0));
    if (grows && q_max > 0.0)
        size *= std::max(0.0, 1.0 - std::abs(q) / q_max);

    if (size < 1.0) return {};
    const Qty qty = static_cast<Qty>(
        std::min(static_cast<double>(p_.max_order_size), size));
    return { submit(side, OrderType::Market, 0, qty) };
}
