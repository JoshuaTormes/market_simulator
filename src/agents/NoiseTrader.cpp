#include "NoiseTrader.h"
#include <algorithm>
#include <cmath>

NoiseTrader::NoiseTrader(AgentId id, const std::string& ticker,
                         std::mt19937_64 rng, Params p,
                         RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , act_u_(0.0, 1.0)
    , side_u_(0.0, 1.0)
    , size_dist_(p.size_mu, p.size_sigma)
{}

std::vector<Action> NoiseTrader::on_market_data(const AgentSnapshot& snap) {
    // The draw is unconditional; the session shape only moves the bar.
    if (act_u_(rng_) >= p_.p_act * p_.intraday.multiplier(snap.base.tick)) return {};

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
