#include "LiquidityConsumerAgent.h"

LiquidityConsumerAgent::LiquidityConsumerAgent(
    uint64_t id,
    double cash,
    int maxSize,
    int targetPosition,
    int tolerance
)
    : AgentBase(id, cash, maxSize),
      targetPosition(targetPosition),
      tolerance(tolerance) {}

std::vector<Order> LiquidityConsumerAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    if (snapshot.bestAsk <= 0.0 || snapshot.bestBid <= 0.0)
        return orders;

    Position pos = getPosition(ticker);

    if (pos.qty < targetPosition) {
        int desired = targetPosition - pos.qty;
        int qty = std::min({desired, maxOrderSize, static_cast<int>(cash / snapshot.bestAsk)});
        if (qty > 0) {
            if (auto o = criarOrdemCompra(ticker, snapshot.bestAsk, qty, tick))
                orders.push_back(*o);
        }
        return orders;
    }

    if (pos.qty > targetPosition + tolerance) {
        int excess = pos.qty - targetPosition;
        int qty = std::min(excess, maxOrderSize);
        if (qty > 0) {
            if (auto o = criarOrdemVenda(ticker, snapshot.bestBid, qty, tick))
                orders.push_back(*o);
        }
    }

    return orders;
}
