#pragma once
// Trend-following via dual moving average crossover + ATR-based stop loss.
// Strategy follows Jegadeesh & Titman (1993) cross-sectional momentum.
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

    int  position_  = 0;  // +1 long, -1 short, 0 flat
    Price entry_px_ = 0;
    double prev_fast_ = 0.0;
    double prev_slow_ = 0.0;
    bool  warm_       = false;
    int   ticks_seen_ = 0;
};
