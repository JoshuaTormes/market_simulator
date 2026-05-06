#pragma once
#include "AgentBase.h"
#include <vector>

class TrendFollowerAgent : public AgentBase {
    int lookback;

public:
    TrendFollowerAgent(uint64_t id, double cash, int maxOrderSize, int lookback);

    std::vector<Order> analisar(
        const MarketSnapshot& snapshot,
        uint64_t timestamp,
        const std::string& ticker
    ) override;

    const char* type() const override { return "TrendFollower"; }
};
