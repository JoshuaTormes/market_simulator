#include "ValueInvestor.h"
#include <cmath>
#include <algorithm>

ValueInvestor::ValueInvestor(AgentId id, const std::string& ticker,
                             std::mt19937_64 rng, Params p,
                             RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
{}

std::vector<Action> ValueInvestor::on_market_data(const AgentSnapshot& snap) {
    if (!snap.has_fundamental) return {};

    double fundamental = snap.fundamental_value;
    double price       = static_cast<double>(snap.perceived_mid);
    if (price <= 0.0) return {};

    double signal = fundamental - price;
    if (std::abs(signal) < p_.min_signal) return {};

    // Target position proportional to mispricing; move kappa fraction toward target each tick.
    double target  = p_.kappa * signal;
    double delta   = target - static_cast<double>(position_);
    Qty    order_q = static_cast<Qty>(std::abs(std::round(delta)));
    order_q = std::clamp(order_q, Qty{1}, p_.max_size);

    Side side = (delta > 0.0) ? Side::Buy : Side::Sell;
    Qty signed_delta = (side == Side::Buy) ? order_q : -order_q;
    position_ += signed_delta;

    return { submit(side, OrderType::Market, 0, order_q) };
}
