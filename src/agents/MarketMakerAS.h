#pragma once
// Market maker: Avellaneda & Stoikov (2008) + Glosten-Milgrom Bayesian belief update.
// Quotes around a private belief μ̂ driven by order flow; spread widens with adverse selection.
#include "AgentBase.h"

class MarketMakerAS : public AgentBase {
public:
    struct Params {
        double gamma       = 0.1;   // risk aversion coefficient
        double kappa       = 1.5;   // order book depth / fill rate parameter
        double sigma       = 0.02;  // volatility estimate (in tick units)
        double T           = 100.0; // initial time horizon in ticks
        Qty    qty         = 10;    // lots per quote
        double beta_ofi    = 0.1;   // Bayesian belief update speed from OFI signal
        double belief_decay = 0.01; // rate at which belief reverts to perceived mid
        double adverse_sel = 0.05;  // adverse selection multiplier on |ofi_tick|
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
    double belief_     = 0.0; // Bayesian mid estimate μ̂ (in tick units)
};
