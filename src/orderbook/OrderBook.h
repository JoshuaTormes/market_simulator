#pragma once
#include <map>
#include <queue>
#include <vector>
#include <unordered_map>
#include "Order.h"
#include "Trade.h"

struct LiquidityState {
    int bidDepth;
    int askDepth;
    int bidLevels;
    int askLevels;
};

struct OrderBookView {
    const std::map<double, std::queue<Order>>* bids;
    const std::map<double, std::queue<Order>>* asks;
};

struct OrderBookLevel {
    double price;
    int quantity;
};

class OrderBook {
public:
    void addOrder(const Order& order, uint64_t insertionTick);
    std::vector<Trade> match(uint64_t timestamp);
    std::vector<Trade> executeMarket(Order order, uint64_t timestamp);

    double bestBid() const;
    double bestAsk() const;
    void cancelOrdersByAgent(uint64_t agentId);

    std::vector<OrderBookLevel> getBids() const;
    std::vector<OrderBookLevel> getAsks() const;
    LiquidityState liquidity() const;

    OrderBookView view() const;

private:
    void removeExpired(uint64_t currentTick);

    std::map<double, std::queue<Order>> bids;
    std::map<double, std::queue<Order>> asks;
    std::unordered_map<uint64_t, uint64_t> insertionTicks;
};
