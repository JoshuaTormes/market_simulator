#pragma once
#include <cstdint>

struct Candle {
    uint64_t index;
    uint64_t startTick; 
    double open;
    double high;
    double low;
    double close;
    uint64_t volume;
};