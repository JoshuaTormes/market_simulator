#include "TrendFollowerAgent.h"

TrendFollowerAgent::TrendFollowerAgent(uint64_t id, double cash, int maxSize, int lookback)
    : AgentBase(id, cash, maxSize), lookback(lookback) {}

std::optional<Order> TrendFollowerAgent::analisar(const MarketSnapshot& snapshot, uint64_t timestamp, const std::string& ticker) {
    Position pos = getPosition(ticker);

    if (snapshot.recentPrices.size() < static_cast<size_t>(lookback)) {
        return std::nullopt;
    }

    double first = snapshot.recentPrices[snapshot.recentPrices.size() - lookback];
    double last = snapshot.recentPrices.back();

    if (last > first) return criarOrdemCompra(ticker, snapshot.bestAsk, maxOrderSize, timestamp);
    if (last < first && getPosition(ticker).qty > 0)
    return criarOrdemVenda(ticker, snapshot.bestBid, getPosition(ticker).qty, timestamp);


    return std::nullopt;
}
