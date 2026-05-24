#pragma once
// Informed trader with linear price impact: Kyle (1985).
#include "AgentBase.h"

class InformedTraderKyle : public AgentBase {
public:
    struct Params {
        double lambda_inv     = 0.5;    // inverse of Kyle's λ: order size per unit of signal
        double signal_noise   = 0.005;  // N(0,σ) noise added to perceived fundamental
        double min_signal     = 2.0;    // minimum |signal| in ticks to act (avoids noise trading)
        Qty    max_order_size = 50;
        double pareto_alpha   = 1.5;    // Pareto tail exponent for order sizing (0 = Kyle only)
    };

    InformedTraderKyle(AgentId id, const std::string& ticker,
                       std::mt19937_64 rng, Params p,
                       RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "InformedTraderKyle"; }

private:
    Params p_;
    std::normal_distribution<double> noise_;
};
