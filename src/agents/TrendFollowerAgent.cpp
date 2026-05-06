#include "TrendFollowerAgent.h"

TrendFollowerAgent::TrendFollowerAgent(uint64_t id, double cash, int maxSize, int lookback)
    : AgentBase(id, cash, maxSize), lookback(lookback) {}

std::vector<Order> TrendFollowerAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t timestamp,
    const std::string& ticker
) {
    std::vector<Order> orders;

    const double trendEnterThreshold = 0.003;
    const double imbalanceEnterThreshold = 0.15;
    const double imbalanceExitThreshold = -0.2;
    const double maxSpreadPressure = 0.004;
    const double maxLiquidityStressEnter = 0.7;
    const double maxLiquidityStressExit = 0.8;
    const double takeProfitPct = 0.01;

    Position pos = getPosition(ticker);
    double avgPrice = getAvgEntryPrice(ticker);

    if (snapshot.recentPrices.size() < static_cast<size_t>(lookback))
        return orders;

    double first = snapshot.recentPrices[snapshot.recentPrices.size() - lookback];
    double last = snapshot.recentPrices.back();

    if (first <= 0.0)
        return orders;

    double trendStrength = (last - first) / first;

    bool upTrend = trendStrength > trendEnterThreshold;
    bool downTrend = trendStrength < -trendEnterThreshold;

    bool spreadOk = snapshot.spreadPressure < maxSpreadPressure;
    bool liquidityOk = snapshot.liquidityStress < maxLiquidityStressEnter;

    if (pos.qty > 0 && avgPrice > 0.0) {
        double pnlPct = (snapshot.lastPrice - avgPrice) / avgPrice;

        bool structureAgainst =
            snapshot.imbalance < imbalanceExitThreshold ||
            snapshot.liquidityStress > maxLiquidityStressExit ||
            downTrend;

        if (pnlPct > takeProfitPct || structureAgainst) {
            int qty = std::min<int64_t>(pos.qty, maxOrderSize);
            if (qty > 0 && snapshot.bestBid > 0.0) {
                if (auto o = criarOrdemVenda(
                        ticker,
                        snapshot.bestBid,
                        qty,
                        timestamp
                    )) {
                    orders.push_back(*o);
                    return orders;
                }
            }
        }
    }

    if (pos.qty == 0) {
        bool structureSupportsBuy =
            upTrend &&
            snapshot.imbalance > imbalanceEnterThreshold &&
            spreadOk &&
            liquidityOk;

        if (structureSupportsBuy && snapshot.bestAsk > 0.0) {
            if (auto o = criarOrdemCompra(
                    ticker,
                    snapshot.bestAsk,
                    maxOrderSize,
                    timestamp
                )) {
                    orders.push_back(*o);
            }
        }
    }

    return orders;
}
