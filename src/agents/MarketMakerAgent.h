#pragma once
#include "AgentBase.h"

class MarketMakerAgent : public AgentBase {
public:
    MarketMakerAgent(
        uint64_t id,
        double cash,
        int maxSize,
        double baseSpread,
        double inventoryLimit,
        double panicRatio
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "MarketMaker"; }

private:
    double baseSpread;
    double inventoryLimit;
    double panicRatio;
};
