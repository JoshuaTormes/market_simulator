#include "RandomAgent.h"
#include <cstdlib>

RandomAgent::RandomAgent(uint64_t id, double cash, int maxQty)
    : AgentBase(id, cash, maxQty), maxQty(maxQty) {}

std::optional<Order> RandomAgent::analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) {
    Position pos = getPosition(ticker);

    Side side = (rand() % 2 == 0) ? Side::Buy : Side::Sell;
    int qty = (rand() % maxQty) + 1;

    double price = snapshot.lastPrice;
    double delta = price * ((rand() % 100) / 10000.0 - 0.005);
    price += delta;

    if (side == Side::Buy) return criarOrdemCompra(ticker, price, qty, tick);
    if (side == Side::Sell && pos.qty > 0) {
        qty = std::min<int64_t>(static_cast<int64_t>(qty), pos.qty);
        return criarOrdemVenda(ticker, price, qty, tick);
    }

    return std::nullopt;
}

