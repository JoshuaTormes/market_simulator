#pragma once
#include <cstdint>
#include <string>

enum class Side {
    Buy,
    Sell
};

struct Order {
    uint64_t id;
    uint64_t agentId;
    Side side;
    double price;
    int quantity;
    uint64_t timestamp;
    std::string ticker;
};
