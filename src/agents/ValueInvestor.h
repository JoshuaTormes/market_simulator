#pragma once
// Value investor: target_position = K × (fundamental − price); converges gradually.
// Motivated by rational expectations equilibrium (Grossman & Stiglitz 1980).
#include "AgentBase.h"

class ValueInvestor : public AgentBase {
public:
    struct Params {
        double kappa      = 0.02;   // speed of adjustment toward target each tick
        double min_signal = 3.0;    // minimum |fundamental − price| in ticks to act
        Qty    max_size   = 50;
    };

    ValueInvestor(AgentId id, const std::string& ticker,
                  std::mt19937_64 rng, Params p,
                  RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "ValueInvestor"; }

private:
    Params p_;
    Qty    position_ = 0;  // tracked internally (approximate)
};
