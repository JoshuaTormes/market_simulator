#pragma once
// Mean-reversion via OU z-score threshold; parameters estimated from rolling window.
// Theoretical basis: Ornstein-Uhlenbeck process (Vasicek 1977; Avellaneda & Lee 2010).
#include "AgentBase.h"
#include "core/RollingWindow.h"

class MeanReverterOU : public AgentBase {
public:
    struct Params {
        int    window    = 40;    // ticks for rolling mean/std
        double entry_z   = 2.0;   // |z| threshold to enter
        double exit_z    = 0.5;   // |z| threshold to exit
        Qty    qty       = 10;
    };

    MeanReverterOU(AgentId id, const std::string& ticker,
                   std::mt19937_64 rng, Params p,
                   RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "MeanReverterOU"; }

private:
    Params p_;
    RollingWindow<double> win_;
    int  position_ = 0;  // +1 long, -1 short, 0 flat
    int  ticks_    = 0;
};
