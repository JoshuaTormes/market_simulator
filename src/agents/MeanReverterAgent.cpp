#include "MeanReverterAgent.h"
#include <numeric>

MeanReverterAgent::MeanReverterAgent(uint64_t id, double cash, int maxSize, int lookback, double threshold)
    : AgentBase(id, cash, maxSize), lookback(lookback), threshold(threshold) {}

std::optional<Order> MeanReverterAgent::analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) {
    if (snapshot.recentPrices.size() < static_cast<size_t>(lookback)) return std::nullopt;

    double sum = std::accumulate(snapshot.recentPrices.end()-lookback, snapshot.recentPrices.end(), 0.0);
    double sma = sum / lookback;
    double price = snapshot.lastPrice;

    if (price < sma * (1.0 - threshold)) {
        int qty = maxOrderSize;
        if (cash >= price * qty)
            return criarOrdemCompra(ticker, price, qty, tick);
    }
    else if (price > sma * (1.0 + threshold)) {
        Position pos = getPosition(ticker);
        if (pos.qty > 0)
            return criarOrdemVenda(ticker, price, pos.qty, tick);
    }

    return std::nullopt;
}
