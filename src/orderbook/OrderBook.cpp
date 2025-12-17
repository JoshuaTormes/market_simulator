#include "OrderBook.h"

void OrderBook::addOrder(const Order& order) {
    if (order.side == Side::Buy) {
        bids[order.price].push(order);
    } else {
        asks[order.price].push(order);
    }
}

std::vector<Trade> OrderBook::match(uint64_t timestamp) {
    std::vector<Trade> trades;

    while (!bids.empty() && !asks.empty()) {
        auto bestBidIt = bids.begin();
        auto bestAskIt = asks.begin();

        if (bestBidIt->first < bestAskIt->first) {
            break;
        }

        Order buy = bestBidIt->second.front();
        Order sell = bestAskIt->second.front();

        int qty = std::min(buy.quantity, sell.quantity);
        double price = bestAskIt->first;

        trades.push_back({
            price,
            qty,
            buy.id,
            sell.id,
            timestamp
        });

        buy.quantity -= qty;
        sell.quantity -= qty;

        bestBidIt->second.pop();
        bestAskIt->second.pop();

        if (buy.quantity > 0) {
            bids[buy.price].push(buy);
        }
        if (sell.quantity > 0) {
            asks[sell.price].push(sell);
        }

        if (bestBidIt->second.empty()) {
            bids.erase(bestBidIt);
        }
        if (bestAskIt->second.empty()) {
            asks.erase(bestAskIt);
        }
    }

    return trades;
}

double OrderBook::bestBid() const {
    if (bids.empty()) return 0.0;
    return bids.begin()->first;
}

double OrderBook::bestAsk() const {
    if (asks.empty()) return 0.0;
    return asks.begin()->first;
}
