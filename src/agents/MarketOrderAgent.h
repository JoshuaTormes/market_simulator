#pragma once
#include "AgentBase.h"
#include <random> 

class MarketOrderAgent : public AgentBase {
public:
    MarketOrderAgent(uint64_t id, double cash, int maxSize);

    std::optional<Order> analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) override;
    const char* type() const override { return "MarketOrderAgent"; }

private:
    std::mt19937 rng;
    std::uniform_real_distribution<> dist;
};
