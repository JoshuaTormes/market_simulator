#pragma once
#include "AgentBase.h"
#include <random>

class ShockAgent : public AgentBase {
public:
    ShockAgent(
        uint64_t id,
        double cash,
        int maxSize,
        int shockInterval
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "ShockAgent"; }

private:
    int shockInterval;
    std::mt19937 rng;
    std::uniform_real_distribution<double> dist;
};
