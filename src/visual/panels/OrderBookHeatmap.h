#pragma once
// Order book depth heatmap: price-offset × time matrix coloured by cumulative qty.
#include "marketdata/MarketSnapshot.h"
#include <array>

class OrderBookHeatmap {
public:
    static constexpr int kHalfRows = 10;  // ±10 ticks around mid
    static constexpr int kPriceRows = 2 * kHalfRows + 1;
    static constexpr int kHistCols  = 60; // last 60 snapshots

    void draw(const MarketSnapshot& snap);

private:
    // Row-major: data_[row * kHistCols + col]
    std::array<float, kPriceRows * kHistCols> data_{};
    float max_qty_ = 1.0f;

    void shift_and_fill(const MarketSnapshot& snap);
};
