#include "MarketOrderAgent.h"

MarketOrderAgent::MarketOrderAgent(uint64_t id, double cash, int maxSize)
    : AgentBase(id, cash, maxSize), dist(0.0, 1.0) {
    rng.seed(id * 1337);
}

std::vector<Order> MarketOrderAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    const double baseBuyChance = 0.04;
    const double baseSellChance = 0.03;
    const double maxSpreadPressure = 0.006;
    const double epsilon = 0.01;

    if (snapshot.spreadPressure > maxSpreadPressure)
        return orders;

    double liquidityFactor = 1.0 - snapshot.liquidityStress;
    double imbalanceBias = snapshot.imbalance;

    double buyChance =
        baseBuyChance * liquidityFactor * (1.0 + std::max(0.0, imbalanceBias));

    double sellChance =
        baseSellChance * liquidityFactor * (1.0 + std::max(0.0, -imbalanceBias));

    double pAct = dist(rng);

    double bid = snapshot.bestBid > 0.0 ? snapshot.bestBid : snapshot.lastPrice;
    double ask = snapshot.bestAsk > 0.0 ? snapshot.bestAsk : snapshot.lastPrice;

    if (pAct < buyChance) {
        int qty = std::min(maxOrderSize, static_cast<int>(cash / ask));
        if (qty > 0) {
            double price = ask * (1.0 - dist(rng) * epsilon);
            if (auto o = criarOrdemCompra(ticker, price, qty, tick))
                orders.push_back(*o);
        }
    } else if (pAct < buyChance + sellChance) {
        Position pos = getPosition(ticker);
        if (pos.qty > 0) {
            int qty = std::min<int64_t>(pos.qty, maxOrderSize);
            double price = bid * (1.0 + dist(rng) * epsilon);
            if (auto o = criarOrdemVenda(ticker, price, qty, tick))
                orders.push_back(*o);
        }
    }

    return orders;
}
