#include "OrderBook.h"
#include <algorithm>
#include <iterator>
#include <unordered_map>

void OrderBook::addOrder(const Order& order, uint64_t insertionTick) {
    insertionTicks[order.id] = insertionTick;
    if (order.side == Side::Buy) {
        bids[order.price].push(order);
    } else {
        asks[order.price].push(order);
    }
}

void OrderBook::removeExpired(uint64_t currentTick) {
    const uint64_t TTL = 60;

    for (auto it = bids.begin(); it != bids.end(); ) {
        auto& q = it->second;
        while (!q.empty()) {
            const Order& o = q.front();
            auto fit = insertionTicks.find(o.id);
            if (fit == insertionTicks.end() || fit->second + TTL < currentTick) {
                insertionTicks.erase(o.id);
                q.pop();
            } else break;
        }
        if (q.empty()) it = bids.erase(it);
        else ++it;
    }

    for (auto it = asks.begin(); it != asks.end(); ) {
        auto& q = it->second;
        while (!q.empty()) {
            const Order& o = q.front();
            auto fit = insertionTicks.find(o.id);
            if (fit == insertionTicks.end() || fit->second + TTL < currentTick) {
                insertionTicks.erase(o.id);
                q.pop();
            } else break;
        }
        if (q.empty()) it = asks.erase(it);
        else ++it;
    }
}

std::vector<Trade> OrderBook::match(uint64_t timestamp) {
    std::vector<Trade> trades;
    removeExpired(timestamp);

    while (!bids.empty() && !asks.empty()) {
        auto bidIt = std::prev(bids.end());
        auto askIt = asks.begin();

        if (bidIt->first < askIt->first) break;

        Order buy = bidIt->second.front();
        Order sell = askIt->second.front();

        if (buy.agentId == sell.agentId) {
            bidIt->second.pop();
            askIt->second.pop();
            insertionTicks.erase(buy.id);
            insertionTicks.erase(sell.id);
            if (bidIt->second.empty()) bids.erase(bidIt);
            if (askIt->second.empty()) asks.erase(askIt);
            continue;
        }

        int qty = std::min(buy.quantity, sell.quantity);
        double price = askIt->first;

        trades.push_back({ price, qty, buy.id, sell.id, timestamp });

        buy.quantity -= qty;
        sell.quantity -= qty;

        bidIt->second.pop();
        askIt->second.pop();

        if (buy.quantity > 0) bids[buy.price].push(buy);
        else insertionTicks.erase(buy.id);

        if (sell.quantity > 0) asks[sell.price].push(sell);
        else insertionTicks.erase(sell.id);

        if (bidIt->second.empty()) bids.erase(bidIt);
        if (askIt->second.empty()) asks.erase(askIt);
    }

    return trades;
}

std::vector<Trade> OrderBook::executeMarket(Order order, uint64_t timestamp) {
    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        while (order.quantity > 0 && !asks.empty()) {
            auto askIt = asks.begin();
            auto& q = askIt->second;
            Order sell = q.front();

            if (sell.agentId == order.agentId) {
                q.pop();
                insertionTicks.erase(sell.id);
                if (q.empty()) asks.erase(askIt);
                continue;
            }

            int qty = std::min(order.quantity, sell.quantity);

            trades.push_back({
                askIt->first,
                qty,
                order.id,
                sell.id,
                timestamp
            });

            order.quantity -= qty;
            sell.quantity -= qty;

            q.pop();
            if (sell.quantity > 0) q.push(sell);
            else insertionTicks.erase(sell.id);

            if (q.empty()) asks.erase(askIt);
        }
    } else {
        while (order.quantity > 0 && !bids.empty()) {
            auto bidIt = std::prev(bids.end());
            auto& q = bidIt->second;
            Order buy = q.front();

            if (buy.agentId == order.agentId) {
                q.pop();
                insertionTicks.erase(buy.id);
                if (q.empty()) bids.erase(bidIt);
                continue;
            }

            int qty = std::min(order.quantity, buy.quantity);

            trades.push_back({
                bidIt->first,
                qty,
                buy.id,
                order.id,
                timestamp
            });

            order.quantity -= qty;
            buy.quantity -= qty;

            q.pop();
            if (buy.quantity > 0) q.push(buy);
            else insertionTicks.erase(buy.id);

            if (q.empty()) bids.erase(bidIt);
        }
    }

    return trades;
}

double OrderBook::bestBid() const {
    if (bids.empty()) return 0.0;
    return std::prev(bids.end())->first;
}

double OrderBook::bestAsk() const {
    if (asks.empty()) return 0.0;
    return asks.begin()->first;
}

void OrderBook::cancelOrdersByAgent(uint64_t agentId) {
    for (auto it = bids.begin(); it != bids.end(); ) {
        std::queue<Order> nq;
        auto& q = it->second;
        while (!q.empty()) {
            if (q.front().agentId != agentId) nq.push(q.front());
            else insertionTicks.erase(q.front().id);
            q.pop();
        }
        if (nq.empty()) it = bids.erase(it);
        else { it->second = std::move(nq); ++it; }
    }

    for (auto it = asks.begin(); it != asks.end(); ) {
        std::queue<Order> nq;
        auto& q = it->second;
        while (!q.empty()) {
            if (q.front().agentId != agentId) nq.push(q.front());
            else insertionTicks.erase(q.front().id);
            q.pop();
        }
        if (nq.empty()) it = asks.erase(it);
        else { it->second = std::move(nq); ++it; }
    }
}

std::vector<OrderBookLevel> OrderBook::getBids() const {
    std::vector<OrderBookLevel> levels;
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        int qty = 0;
        auto q = it->second;
        while (!q.empty()) { qty += q.front().quantity; q.pop(); }
        levels.push_back({ it->first, qty });
    }
    return levels;
}

std::vector<OrderBookLevel> OrderBook::getAsks() const {
    std::vector<OrderBookLevel> levels;
    for (auto it = asks.begin(); it != asks.end(); ++it) {
        int qty = 0;
        auto q = it->second;
        while (!q.empty()) { qty += q.front().quantity; q.pop(); }
        levels.push_back({ it->first, qty });
    }
    return levels;
}

LiquidityState OrderBook::liquidity() const {
    LiquidityState s{0, 0, 0, 0};

    for (const auto& [p, q] : bids) {
        s.bidLevels++;
        auto t = q;
        while (!t.empty()) { s.bidDepth += t.front().quantity; t.pop(); }
    }

    for (const auto& [p, q] : asks) {
        s.askLevels++;
        auto t = q;
        while (!t.empty()) { s.askDepth += t.front().quantity; t.pop(); }
    }

    return s;
}

OrderBookView OrderBook::view() const {
    return OrderBookView{ &bids, &asks };
}
