#include "StopLossCluster.h"
#include <cmath>

StopLossCluster::StopLossCluster(AgentId id, const std::string& ticker,
                                 std::mt19937_64 rng, Params p,
                                 RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
{
    double dist = p.trigger_pct * static_cast<double>(p.entry_price);
    if (p.initial_side == Side::Buy)
        stop_level_ = p.entry_price - static_cast<Price>(std::round(dist));
    else
        stop_level_ = p.entry_price + static_cast<Price>(std::round(dist));
}

std::vector<Action> StopLossCluster::on_market_data(const AgentSnapshot& snap) {
    if (triggered_) return {};
    if (snap.base.spread < 0) return {};

    Price mid = snap.perceived_mid;
    bool breached = (p_.initial_side == Side::Buy)  ? (mid <= stop_level_)
                                                     : (mid >= stop_level_);
    if (!breached) return {};

    triggered_ = true;
    // Exit the position: sell if long, buy if short.
    Side exit_side = (p_.initial_side == Side::Buy) ? Side::Sell : Side::Buy;
    return { submit(exit_side, OrderType::Market, 0, p_.qty) };
}
