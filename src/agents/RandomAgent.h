#pragma once
#include "AgentBase.h"

class RandomAgent : public AgentBase {
public:
    RandomAgent(uint64_t id, double cash, int maxQty);

    std::optional<Order> analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) override;
    const char* type() const override { return "RandomAgent"; }

private:
    int maxQty;
};
