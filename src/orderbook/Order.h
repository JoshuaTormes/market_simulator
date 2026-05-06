#pragma once
#include <cstdint>
#include <string>

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

struct Order {
    uint64_t id;
    uint64_t agentId;
    Side side;
    OrderType type;
    double price;
    int quantity;
    uint64_t timestamp;
    std::string ticker;
};
