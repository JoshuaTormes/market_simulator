#pragma once
#include "AgentBase.h"

class ContrarianAgent : public AgentBase {
public:
    ContrarianAgent(
        uint64_t id,
        double cash,
        int maxSize,
        double pressureThreshold
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "Contrarian"; }

private:
    double pressureThreshold;
};
