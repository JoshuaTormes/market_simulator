#pragma once
#include "AgentBase.h"
#include <vector>

class ValueAgent : public AgentBase {
public:
    ValueAgent(
        uint64_t id,
        double cash,
        int maxSize,
        double threshold
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "ValueAgent"; }

private:
    double threshold;
};
