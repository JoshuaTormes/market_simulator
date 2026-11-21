#pragma once
// Intraday trend follower: dual moving average crossover + ATR-based stop.
// The windows are tens of ticks (seconds), so this is chartist trend
// following.  It is not the cross-sectional momentum of Jegadeesh & Titman
// (1993), which ranks stocks on 3-12 month returns.
#include "AgentBase.h"
#include "core/RollingWindow.h"
#include <deque>

class MomentumTrader : public AgentBase {
public:
    struct Params {
        int  fast_window  = 10;   // ticks for fast MA
        int  slow_window  = 30;   // ticks for slow MA
        int  atr_window   = 14;   // ticks for ATR stop
        double atr_mult   = 2.0;  // stop = entry ± atr_mult * ATR
        Qty  qty          = 10;
    };

    MomentumTrader(AgentId id, const std::string& ticker,
                   std::mt19937_64 rng, Params p,
                   RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "MomentumTrader"; }

private:
    Params p_;
    RollingWindow<double> fast_ma_;
    RollingWindow<double> slow_ma_;
    RollingWindow<double> atr_win_;

    double prev_fast_ = 0.0;
    double prev_slow_ = 0.0;
    bool  warm_       = false;
    int   ticks_seen_ = 0;
};
