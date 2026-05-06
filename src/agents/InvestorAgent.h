#pragma once
#include "AgentBase.h"

class InvestorAgent : public AgentBase {
public:
    InvestorAgent(
        uint64_t id,
        double cash,
        int maxSize,
        double takeProfit,
        double stopLoss
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t tick,
        const std::string& ticker
    ) override;

    const char* type() const override { return "Investor"; }

private:
    double takeProfit;
    double stopLoss;
};
