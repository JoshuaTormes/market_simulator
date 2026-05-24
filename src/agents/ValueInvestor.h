#pragma once
// Value investor: slow mean-reverter using a noisy private belief about fair value.
// No access to the fundamental — belief is an EMA of observed prices plus noise.
#include "AgentBase.h"
#include <random>

class ValueInvestor : public AgentBase {
public:
    struct Params {
        double kappa      = 0.02;   // speed of adjustment toward target each tick
        double min_signal = 3.0;    // minimum |belief − price| in ticks to act
        Qty    max_size   = 50;
        double belief_alpha = 0.001; // EMA weight for fair-value belief update (slow)
        double belief_noise = 0.5;   // stdev of per-tick noise added to belief (in ticks)
    };

    ValueInvestor(AgentId id, const std::string& ticker,
                  std::mt19937_64 rng, Params p,
                  RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "ValueInvestor"; }

private:
    Params p_;
    Qty    position_   = 0;     // tracked internally (approximate)
    double belief_     = 0.0;   // internal fair-value belief (ticks), initialized on first tick
    std::normal_distribution<double> belief_noise_dist_;
};
