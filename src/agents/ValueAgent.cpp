#include "ValueAgent.h"

ValueAgent::ValueAgent(
    uint64_t id,
    double cash,
    int maxSize,
    double threshold
)
    : AgentBase(id, cash, maxSize),
      threshold(threshold) {}

std::vector<Order> ValueAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    const double maxSpreadPressure = 0.005;
    const double maxLiquidityStressEnter = 0.75;
    const double maxLiquidityStressExit = 0.85;
    const double imbalanceEnter = 0.1;
    const double imbalanceExit = -0.25;

    if (snapshot.perceivedPrice <= 0.0)
        return orders;

    Position pos = getPosition(ticker);
    double avgPrice = getAvgEntryPrice(ticker);

    double valueGap =
        (snapshot.lastPrice - snapshot.perceivedPrice) / snapshot.perceivedPrice;

    bool spreadOk = snapshot.spreadPressure < maxSpreadPressure;
    bool liquidityOk = snapshot.liquidityStress < maxLiquidityStressEnter;

    if (pos.qty > 0 && avgPrice > 0.0) {
        double pnlPct = (snapshot.lastPrice - avgPrice) / avgPrice;

        bool structureAgainst =
            snapshot.imbalance < imbalanceExit ||
            snapshot.liquidityStress > maxLiquidityStressExit;

        if (pnlPct > threshold || structureAgainst) {
            int qty = std::min<int64_t>(pos.qty, maxOrderSize);
            if (qty > 0 && snapshot.bestBid > 0.0) {
                if (auto o = criarOrdemVenda(
                        ticker,
                        snapshot.bestBid,
                        qty,
                        tick
                    )) {
                    orders.push_back(*o);
                    return orders;
                }
            }
        }
    }

    if (pos.qty == 0) {
        bool structureSupportsBuy =
            valueGap < -threshold &&
            snapshot.imbalance > imbalanceEnter &&
            spreadOk &&
            liquidityOk;

        if (structureSupportsBuy && snapshot.bestAsk > 0.0) {
            int qty = std::min(
                maxOrderSize,
                static_cast<int>(cash / snapshot.bestAsk)
            );
            if (qty > 0) {
                if (auto o = criarOrdemCompra(
                        ticker,
                        snapshot.bestAsk,
                        qty,
                        tick
                    )) {
                        orders.push_back(*o);
                }
            }
        }
    }

    return orders;
}
