#pragma once
// Stop-loss cluster: fires a liquidation market order when the unrealized loss
// on its position breaches a threshold, then re-enters after a cooldown.
//
// The re-entry is the point.  A cluster that latches `triggered_` forever fires
// once in the first few hundred ticks and is inert for the remaining 50k, so
// the cascade mechanism it models (Cont & Wagalath 2013) is absent from all but
// the opening of every run.  Real stop clusters are continuously replenished:
// positions are re-established at new prices, and each new cohort brings its
// own stop level.  Here that is a cooldown followed by a fresh market entry at
// a randomly drawn side and size, with the stop measured from the fill.
#include "AgentBase.h"
#include <random>

class StopLossCluster : public AgentBase {
public:
    struct Params {
        Side   initial_side  = Side::Buy;  // direction of the seeded position
        Price  entry_price   = 10000;      // reference price of the seeded position
        double trigger_pct   = 0.03;       // stop at entry ± trigger_pct × entry
        Qty    qty           = 50;         // size of the seeded position
        // Re-entry after a stop is hit.
        int    cooldown_ticks = 500;       // ticks flat before a new position
        Qty    qty_min        = 10;        // re-entry size is uniform in
        Qty    qty_max        = 75;        // [qty_min, qty_max]
    };

    StopLossCluster(AgentId id, const std::string& ticker,
                    std::mt19937_64 rng, Params p,
                    RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "StopLossCluster"; }

    // Diagnostics: how many stops have fired over the run.
    int stops_fired() const { return stops_fired_; }

private:
    Params p_;
    std::bernoulli_distribution side_coin_;
    std::uniform_int_distribution<long long> qty_dist_;

    // Live position: armed_ means there is a stop to watch.
    bool   armed_          = true;
    Side   side_           = Side::Buy;
    double entry_ref_      = 0.0;   // reference price in tick units
    Qty    qty_            = 0;
    Tick   cooldown_until_ = 0;
    int    stops_fired_    = 0;
};
