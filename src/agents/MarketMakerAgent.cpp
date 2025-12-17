#include "MarketMakerAgent.h"

MarketMakerAgent::MarketMakerAgent(uint64_t id, int maxOrderSize, double spread)
    : AgentBase(id, 10000.0, maxOrderSize), spread(spread) {}

std::optional<Order> MarketMakerAgent::analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) {
    Position pos = getPosition(ticker);
    double priceBuy = snapshot.lastPrice - spread;

    if (cash >= priceBuy * maxOrderSize) {
        return criarOrdemCompra(ticker, priceBuy, maxOrderSize, tick);
    }

    if (pos.qty > 0) {
        return criarOrdemVenda(ticker, snapshot.lastPrice + spread, pos.qty, tick);
    }

    return std::nullopt;
}
