#pragma once
#include <cstdint>

struct Trade {
    double price;
    int quantity;
    uint64_t buyOrderId;
    uint64_t sellOrderId;
    uint64_t timestamp;
};
