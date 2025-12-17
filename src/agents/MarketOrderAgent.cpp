#include "MarketOrderAgent.h"
#include <random>

MarketOrderAgent::MarketOrderAgent(uint64_t id, double cash, int maxSize)
    : AgentBase(id, cash, maxSize), dist(0.0, 1.0) {
    rng.seed(id);
}

std::optional<Order> MarketOrderAgent::analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) {
    double chance = 0.1;
    if (dist(rng) < chance) {
        int64_t qty = static_cast<int64_t>(maxOrderSize);
        bool buy = dist(rng) < 0.5;

        if (buy && cash >= snapshot.lastPrice * qty)
            return criarOrdemCompra(ticker, snapshot.lastPrice, qty, tick);
        else if (!buy) {
            Position pos = getPosition(ticker);
            if (pos.qty > 0)
                return criarOrdemVenda(ticker, snapshot.lastPrice, std::min(qty, pos.qty), tick);
        }
    }
    return std::nullopt;
}
