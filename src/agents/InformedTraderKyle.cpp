// S        = V · exp(ε),  ε ~ N(0, signal_noise_log)
// gap      = S − mid                                          (price ticks)
// thr      = spread/2 + margin_ticks + deadband_sigma · signal_noise_log · mid
// gap_eff  = sign(gap) · max(0, |gap| − thr)
// q*       = clamp(κ · gap_eff, −q_soft, +q_soft)
// order    = speed · (q* − q)   (× Pareto multiplier when it grows |q|)
// send if |order| ≥ min_order, capped at max_order_size, as a market order
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
    // Both draws happen every tick, whatever the state, so the RNG stream does
    // not depend on the path.
    const double eps = log_noise_(rng_);
    const double u   = u_(rng_);

    if (!snap.has_fundamental) return {};
    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    // Private value: a proportional error on V, so the signal quality does not
    // depend on the price level.
    const double S   = snap.fundamental_value * std::exp(eps);
    const double gap = S - mid;

    // Costs of acting on the gap: crossing the spread, a margin, and one sigma
    // of the trader's own signal noise — a gap inside its own error bar is
    // noise, and trading noise pays the spread to the makers for nothing.
    const double half_spread = 0.5 * static_cast<double>(std::max<Price>(snap.base.spread, 0));
    const double sigma_ticks = p_.signal_noise_log * mid;
    const double lvl = (p_.price_ref_ticks > 0.0) ? mid / p_.price_ref_ticks : 1.0;
    const double thr = half_spread + lvl * p_.margin_ticks + p_.deadband_sigma * sigma_ticks;

    const double excess  = std::max(0.0, std::abs(gap) - thr);
    const double gap_eff = (gap >= 0.0) ? excess : -excess;

    const double q_soft = static_cast<double>(rl_.max_position)
                        * std::clamp(p_.q_soft_frac, 0.05, 1.0);
    target_ = std::clamp(p_.target_lots_per_tick * gap_eff / lvl, -q_soft, q_soft);

    const double q = snap.own_inventory;
    double order = p_.speed * (target_ - q);

    // Heavy tail on the slices that build a position: real informed flow is
    // not a constant drip.  The multiplier has unit mean, so κ and speed keep
    // their meaning; it is not applied when the trader is coming back toward
    // flat, where overshooting would just churn.
    const bool grows = std::abs(q + order) > std::abs(q);
    if (grows && p_.pareto_alpha > 1.0) {
        order *= ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
               * std::pow(1.0 - u, -1.0 / p_.pareto_alpha);
    }

    const double mag = std::abs(order);
    if (mag < static_cast<double>(p_.min_order)) return {};

    const Side side = (order > 0.0) ? Side::Buy : Side::Sell;
    const Qty  qty  = static_cast<Qty>(std::llround(
        std::min(static_cast<double>(p_.max_order_size), mag)));
    if (qty <= 0) return {};
    return { submit(side, OrderType::Market, 0, qty) };
}
