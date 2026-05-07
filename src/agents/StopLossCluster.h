#pragma once
// Cluster of stop-loss orders that trigger a cascade when price breaches a level.
// Models the stop-loss cascade mechanism described in Cont & Wagalath (2013).
#include "AgentBase.h"

class StopLossCluster : public AgentBase {
public:
    struct Params {
        Side  initial_side   = Side::Buy;   // side of the initial position
        Price entry_price    = 10000;       // initial position price (ticks)
        double trigger_pct   = 0.03;        // stop triggers at entry ± trigger_pct * entry
        Qty   qty            = 50;
    };

    StopLossCluster(AgentId id, const std::string& ticker,
                    std::mt19937_64 rng, Params p,
                    RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "StopLossCluster"; }

private:
    Params p_;
    bool   triggered_ = false;
    Price  stop_level_ = 0;
};
