#pragma once
// Uninformed noise trader: random direction, lognormal size.
// Provides baseline liquidity demand; calibrated to Black-Scholes noise process.
#include "AgentBase.h"
#include <random>

class NoiseTrader : public AgentBase {
public:
    struct Params {
        double p_act     = 0.05;  // probability of acting each tick (Bernoulli)
        double size_mu   = 1.0;   // log-mean of order size (lognormal)
        double size_sigma = 0.5;  // log-std of order size
        Qty    max_size  = 200;
    };

    NoiseTrader(AgentId id, const std::string& ticker,
                std::mt19937_64 rng, Params p,
                RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "NoiseTrader"; }

private:
    Params p_;
    std::bernoulli_distribution  act_;
    std::bernoulli_distribution  side_coin_;
    std::lognormal_distribution<double> size_dist_;
};
