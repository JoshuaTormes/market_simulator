#include "RandomAgent.h"
#include <cstdlib>
#include <cmath>

RandomAgent::RandomAgent(uint64_t id, double cash, int maxQty)
    : AgentBase(id, cash, maxQty),
      maxQty(maxQty) {}

std::vector<Order> RandomAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    const double maxSpreadPressure = 0.008;
    const double maxLiquidityStress = 0.8;
    const double epsilon = 0.002;

    if (snapshot.perceivedPrice <= 0.0)
        return orders;

    if (snapshot.spreadPressure > maxSpreadPressure)
        return orders;

    if (snapshot.liquidityStress > maxLiquidityStress)
        return orders;

    Position pos = getPosition(ticker);

    Side side = (rand() % 2 == 0) ? Side::Buy : Side::Sell;
    int qty = (rand() % maxQty) + 1;

    if (side == Side::Buy && snapshot.bestAsk > 0.0) {
        double price =
            snapshot.bestAsk *
            (1.0 - epsilon + (rand() % 100) / 50000.0);

        if (cash >= price * qty) {
            if (auto o = criarOrdemCompra(
                    ticker,
                    price,
                    qty,
                    tick
                )) {
                orders.push_back(*o);
            }
        }
    }

    if (side == Side::Sell && pos.qty > 0 && snapshot.bestBid > 0.0) {
        qty = std::min<int64_t>(pos.qty, qty);

        double price =
            snapshot.bestBid *
            (1.0 + epsilon - (rand() % 100) / 50000.0);

        if (qty > 0) {
            if (auto o = criarOrdemVenda(
                    ticker,
                    price,
                    qty,
                    tick
                )) {
                orders.push_back(*o);
            }
        }
    }

    return orders;
}
