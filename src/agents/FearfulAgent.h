#pragma once
#include "AgentBase.h"
#include <unordered_map>
#include <vector>

class FearfulAgent : public AgentBase {
    double stopLossPct;
    double takeProfitPct;
    std::unordered_map<std::string, double> entryPrices;

public:
    FearfulAgent(
        uint64_t id,
        double cash,
        int maxOrderSize,
        double stopLossPct,
        double takeProfitPct
    );

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t timestamp,
        const std::string& ticker
    ) override;

    const char* type() const override { return "FearfulAgent"; }
};
