#include "MarketMakerAgent.h"
#include <algorithm>
#include <cmath>
#include <iostream>

MarketMakerAgent::MarketMakerAgent(
    uint64_t id,
    double cash,
    int maxSize,
    double baseSpread,
    double inventoryLimit,
    double panicRatio
)
    : AgentBase(id, cash, maxSize),
      baseSpread(baseSpread),
      inventoryLimit(inventoryLimit),
      panicRatio(panicRatio) {}

std::vector<Order> MarketMakerAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;


    

    const double maxSpreadPressure = 0.01;

    if (snapshot.perceivedPrice <= 0.0)
        return orders;

    if (snapshot.spreadPressure > maxSpreadPressure)
        return orders;

    Position pos = getPosition(ticker);
    double ref = snapshot.perceivedPrice;

    double invRatio = std::clamp(
        static_cast<double>(pos.qty) / inventoryLimit,
        -1.0,
        1.0
    );

    double riskFactor =
        1.0 +
        2.5 * std::abs(invRatio) +
        2.0 * snapshot.liquidityStress;

    double spread = baseSpread * riskFactor;

    double skew = snapshot.imbalance * spread * 0.5;

    double bid = ref - spread - skew;
    double ask = ref + spread - skew;

    int buyQty = maxOrderSize;
    int sellQty = maxOrderSize;

    if (invRatio > panicRatio) {
        buyQty = 0;
        sellQty = maxOrderSize * 2;
        ask = ref - spread * 0.4;
    }

    if (invRatio < -panicRatio) {
        sellQty = 0;
        buyQty = maxOrderSize * 2;
        bid = ref + spread * 0.4;
    }

    if (buyQty > 0 && bid > 0.0 && cash >= bid * buyQty) {
        if (auto o = criarOrdemCompra(ticker, bid, buyQty, tick))
            orders.push_back(*o);
    }

    if (sellQty > 0 && ask > 0.0 && pos.qty >= sellQty) {
        if (auto o = criarOrdemVenda(ticker, ask, sellQty, tick))
            orders.push_back(*o);
    }

    return orders;
}
