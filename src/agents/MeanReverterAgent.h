
#pragma once
#include "AgentBase.h"

class MeanReverterAgent : public AgentBase {
public:
    MeanReverterAgent(uint64_t id, double cash, int maxSize, int lookback, double threshold);

    std::optional<Order> analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) override;
    const char* type() const override { return "MeanReverter"; }

private:
    int lookback;
    double threshold; // % do desvio da média
};
