// Ledger-driven path: uses own_inventory and own_avg_cost from AgentSnapshot.
// Fallback path: uses params.entry_price and params.qty (pre-seeded or default).
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

    // Ledger path: when AgentRunner has wired a real position, use P&L-based trigger.
    if (snap.own_avg_cost > 0.0 && snap.own_inventory != 0.0) {
        double ref    = snap.own_avg_cost;
        bool   is_long = snap.own_inventory > 0.0;
        bool   breached = is_long ? (static_cast<double>(mid) < ref * (1.0 - p_.trigger_pct))
                                  : (static_cast<double>(mid) > ref * (1.0 + p_.trigger_pct));
        if (!breached) return {};

        triggered_    = true;
        Side exit     = is_long ? Side::Sell : Side::Buy;
        Qty  liq_qty  = static_cast<Qty>(std::abs(snap.own_inventory));
        if (liq_qty <= 0) liq_qty = p_.qty;
        return { submit(exit, OrderType::Market, 0, liq_qty) };
    }

    // Fallback: price-level trigger using params.entry_price.
    bool breached = (p_.initial_side == Side::Buy) ? (mid <= stop_level_)
                                                    : (mid >= stop_level_);
    if (!breached) return {};

    triggered_ = true;
    Side exit_side = (p_.initial_side == Side::Buy) ? Side::Sell : Side::Buy;
    return { submit(exit_side, OrderType::Market, 0, p_.qty) };
}
