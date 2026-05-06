#pragma once
#include "AgentBase.h"

class CyclicTraderAgent : public AgentBase {
public:
    CyclicTraderAgent(
        uint64_t id,
        double cash,
        int maxSize,
        int tickInterval,
        bool startBuying = true
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "CyclicTrader"; }

private:
    int tickInterval;
    uint64_t lastTradeTick = 0;
    bool buyNext;
};
