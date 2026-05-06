#include "ContrarianAgent.h"

ContrarianAgent::ContrarianAgent(
    uint64_t id,
    double cash,
    int maxSize,
    double imbalanceThreshold
)
    : AgentBase(id, cash, maxSize),
      pressureThreshold(imbalanceThreshold) {}

std::vector<Order> ContrarianAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    Position pos = getPosition(ticker);

    double imbalance = snapshot.imbalance;

    if (imbalance < -pressureThreshold && snapshot.bestAsk > 0.0) {
        int qty = std::min(
            maxOrderSize,
            static_cast<int>(cash / snapshot.bestAsk)
        );
        if (qty > 0) {
            if (auto o = criarOrdemCompra(ticker, snapshot.bestAsk, qty, tick))
                orders.push_back(*o);
        }
    }

    if (imbalance > pressureThreshold && pos.qty > 0 && snapshot.bestBid > 0.0) {
        int qty = std::min<int64_t>(pos.qty, maxOrderSize);
        if (qty > 0) {
            if (auto o = criarOrdemVenda(ticker, snapshot.bestBid, qty, tick))
                orders.push_back(*o);
        }
    }

    return orders;
}
