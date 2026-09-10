#include "NoiseTrader.h"
#include <algorithm>
#include <cmath>

NoiseTrader::NoiseTrader(AgentId id, const std::string& ticker,
                         std::mt19937_64 rng, Params p,
                         RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , act_(p.p_act)
    , side_u_(0.0, 1.0)
    , size_dist_(p.size_mu, p.size_sigma)
{}

std::vector<Action> NoiseTrader::on_market_data(const AgentSnapshot& snap) {
    if (!act_(rng_)) return {};

    Qty size = static_cast<Qty>(std::round(size_dist_(rng_)));
    size = std::clamp(size, Qty{1}, p_.max_size);

    // Tilt the coin against the current inventory.  The draw is taken
    // unconditionally so the RNG stream does not depend on the position.
    const double u  = side_u_(rng_);
    const double qs = (p_.q_scale > 0.0) ? p_.q_scale : 1.0;
    const double p_buy = 0.5 - 0.5 * std::tanh(snap.own_inventory / qs);

    Side side = (u < p_buy) ? Side::Buy : Side::Sell;
    return { submit(side, OrderType::Market, 0, size) };
}
