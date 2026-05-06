#include "FearfulAgent.h"

FearfulAgent::FearfulAgent(
    uint64_t id,
    double cash,
    int maxSize,
    double sl,
    double tp
)
    : AgentBase(id, cash, maxSize),
      stopLossPct(sl),
      takeProfitPct(tp) {}

std::vector<Order> FearfulAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t timestamp,
    const std::string& ticker
) {
    std::vector<Order> orders;

    const double maxSpreadPressure = 0.006;
    const double maxLiquidityStressEnter = 0.65;
    const double maxLiquidityStressExit = 0.8;
    const double panicImbalance = -0.3;

    Position pos = getPosition(ticker);
    double entryPrice = entryPrices[ticker];

    if (pos.qty > 0 && entryPrice > 0.0) {
        double ret =
            (snapshot.perceivedPrice - entryPrice) / entryPrice;

        bool panicStructure =
            snapshot.liquidityStress > maxLiquidityStressExit ||
            snapshot.imbalance < panicImbalance;

        if (ret <= -stopLossPct || ret >= takeProfitPct || panicStructure) {
            entryPrices[ticker] = 0.0;
            if (snapshot.bestBid > 0.0) {
                if (auto o = criarOrdemVenda(
                        ticker,
                        snapshot.bestBid,
                        pos.qty,
                        timestamp
                    )) {
                    orders.push_back(*o);
                }
            }
        }

        return orders;
    }

    if ((timestamp % (5 + (getId() % 3))) != 0)
        return orders;

    bool spreadOk = snapshot.spreadPressure < maxSpreadPressure;
    bool liquidityOk = snapshot.liquidityStress < maxLiquidityStressEnter;

    if (!spreadOk || !liquidityOk)
        return orders;

    int qty = maxOrderSize;

    if (snapshot.bestAsk > 0.0 && cash >= snapshot.bestAsk * qty) {
        if (auto o = criarOrdemCompra(
                ticker,
                snapshot.bestAsk,
                qty,
                timestamp
            )) {
            entryPrices[ticker] = snapshot.perceivedPrice;
            orders.push_back(*o);
        }
    }

    return orders;
}
