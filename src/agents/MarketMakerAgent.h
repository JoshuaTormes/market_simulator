#pragma once
#include "AgentBase.h"

class MarketMakerAgent : public AgentBase {
public:
    MarketMakerAgent(uint64_t id, int maxOrderSize, double spread);

    std::optional<Order> analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) override;
    const char* type() const override { return "MarketMaker"; }

private:
    double spread;
};
