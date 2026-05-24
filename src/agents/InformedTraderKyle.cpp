// Kyle (1985): size = sign(signal) · min(λ⁻¹·|signal|, max_position)
#include "InformedTraderKyle.h"
#include <cmath>
#include <algorithm>

InformedTraderKyle::InformedTraderKyle(AgentId id, const std::string& ticker,
                                       std::mt19937_64 rng, Params p,
                                       RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , noise_(0.0, p.signal_noise)
{}

std::vector<Action> InformedTraderKyle::on_market_data(const AgentSnapshot& snap) {
    if (!snap.has_fundamental) return {};

    double fundamental = snap.fundamental_value + noise_(rng_);
    double mid         = static_cast<double>(snap.perceived_mid);
    double signal      = fundamental - mid;

    if (std::abs(signal) < p_.min_signal) return {};

    double base_size = p_.lambda_inv * std::abs(signal);
    if (p_.pareto_alpha > 0.0) {
        // Pareto multiplier preserves Kyle direction but creates heavy-tailed trade sizes.
        std::uniform_real_distribution<double> u_dist(0.0, 0.9999);
        double u = u_dist(rng_);
        double multiplier = ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
                          * std::pow(1.0 - u, -1.0 / p_.pareto_alpha);
        base_size *= multiplier;
    }
    Qty size = static_cast<Qty>(
        std::min(static_cast<double>(p_.max_order_size), std::max(1.0, base_size)));
    if (size <= 0) return {};

    Side side = (signal > 0.0) ? Side::Buy : Side::Sell;
    return { submit(side, OrderType::Market, 0, size) };
}
