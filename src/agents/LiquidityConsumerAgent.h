#pragma once
#include "AgentBase.h"

class LiquidityConsumerAgent : public AgentBase {
public:
    LiquidityConsumerAgent(
        uint64_t id,
        double cash,
        int maxSize,
        int targetPosition,
        int tolerance
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "LiquidityConsumer"; }

private:
    int targetPosition;
    int tolerance;
};
