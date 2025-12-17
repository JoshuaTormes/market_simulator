#pragma once
#include <map>
#include <queue>
#include <vector>
#include "Order.h"
#include "Trade.h"

class OrderBook {
public:
    void addOrder(const Order& order);
    std::vector<Trade> match(uint64_t timestamp);

    double bestBid() const;
    double bestAsk() const;

private:
    std::map<double, std::queue<Order>, std::greater<double>> bids;
    std::map<double, std::queue<Order>> asks;
};
