#pragma once
// Stop-loss cluster: fires a liquidation market order when unrealized P&L from the
// ledger (own_inventory × own_avg_cost) breaches a loss threshold.
// Models the cascade mechanism described in Cont & Wagalath (2013).
#include "AgentBase.h"

class StopLossCluster : public AgentBase {
public:
    struct Params {
        Side   initial_side = Side::Buy;  // direction of the seeded position
        Price  entry_price  = 10000;      // fallback reference price when ledger has no position
        double trigger_pct  = 0.03;       // stop at entry ± trigger_pct * entry_price
        Qty    qty          = 50;         // fallback qty when ledger has no position
    };

    StopLossCluster(AgentId id, const std::string& ticker,
                    std::mt19937_64 rng, Params p,
                    RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "StopLossCluster"; }

private:
    Params p_;
    bool   triggered_ = false;
    Price  stop_level_ = 0;  // computed from params at construction (fallback path)
};
