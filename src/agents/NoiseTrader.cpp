#include "NoiseTrader.h"
#include <algorithm>
#include <cmath>

NoiseTrader::NoiseTrader(AgentId id, const std::string& ticker,
                         std::mt19937_64 rng, Params p,
                         RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , act_(p.p_act)
    , side_coin_(0.5)
    , size_dist_(p.size_mu, p.size_sigma)
{}

std::vector<Action> NoiseTrader::on_market_data(const AgentSnapshot& /*snap*/) {
    if (!act_(rng_)) return {};

    Qty size = static_cast<Qty>(std::round(size_dist_(rng_)));
    size = std::clamp(size, Qty{1}, p_.max_size);

    Side side = side_coin_(rng_) ? Side::Buy : Side::Sell;
    return { submit(side, OrderType::Market, 0, size) };
}
