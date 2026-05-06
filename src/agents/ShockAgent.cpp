#include "ShockAgent.h"

ShockAgent::ShockAgent(
    uint64_t id,
    double cash,
    int maxSize,
    int shockInterval
)
    : AgentBase(id, cash, maxSize),
      shockInterval(shockInterval),
      dist(0.0, 1.0) {
    rng.seed(id * 99991);
}

std::vector<Order> ShockAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    if (tick % shockInterval != 0)
        return orders;

    double p = dist(rng);
    Position pos = getPosition(ticker);

    if (p < 0.5 && pos.qty > 0 && snapshot.perceivedPrice > 0.0) {
        int qty = std::min<int64_t>(pos.qty, maxOrderSize * 5);
        if (auto o = criarOrdemVenda(
                ticker,
                snapshot.bestBid * 0.98,
                qty,
                tick
            ))
            orders.push_back(*o);
    } else if (p >= 0.5 && snapshot.perceivedPrice > 0.0) {
        int qty = std::min(
            static_cast<int>(cash / snapshot.perceivedPrice),
            maxOrderSize * 5
        );
        if (qty > 0) {
            if (auto o = criarOrdemCompra(
                    ticker,
                    snapshot.bestAsk * 1.02,
                    qty,
                    tick
                ))
                orders.push_back(*o);
        }
    }

    return orders;
}
