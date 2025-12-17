#pragma once
#include <vector>
#include <cstdint>

struct MarketSnapshot {
    double lastPrice;
    double bestBid;
    double bestAsk;
    std::vector<double> recentPrices;
};
