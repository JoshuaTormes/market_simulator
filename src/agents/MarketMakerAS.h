#pragma once
// Market maker using optimal bid-ask spread: Avellaneda & Stoikov (2008).
#include "AgentBase.h"

class MarketMakerAS : public AgentBase {
public:
    struct Params {
        double gamma  = 0.1;   // risk aversion coefficient
        double kappa  = 1.5;   // order book depth / fill rate parameter
        double sigma  = 0.02;  // volatility estimate (annualised in tick units)
        double T      = 100.0; // initial time horizon in ticks
        Qty    qty    = 10;    // lots per quote
    };

    MarketMakerAS(AgentId id, const std::string& ticker,
                  std::mt19937_64 rng, Params p,
                  RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "MarketMakerAS"; }

private:
    Params p_;
    Tick   birth_tick_ = 0;
    bool   first_tick_ = true;
};
