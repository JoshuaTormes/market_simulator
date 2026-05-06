#include "InvestorAgent.h"

InvestorAgent::InvestorAgent(
    uint64_t id,
    double cash,
    int maxSize,
    double takeProfit,
    double stopLoss
)
    : AgentBase(id, cash, maxSize),
      takeProfit(takeProfit),
      stopLoss(stopLoss) {}

std::vector<Order> InvestorAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    if (snapshot.recentPrices.size() < 4)
        return orders;

    double last = snapshot.recentPrices.back();
    double prev = snapshot.recentPrices[snapshot.recentPrices.size() - 4];

    Position pos = getPosition(ticker);
    double avgPrice = getAvgEntryPrice(ticker);

    bool priceRising = last > prev;

    if (pos.qty == 0) {
        if (priceRising && snapshot.bestAsk > 0.0) {
            int qty = std::min(
                maxOrderSize,
                static_cast<int>(cash / snapshot.bestAsk)
            );
            if (qty > 0) {
                if (auto o = criarOrdemCompra(ticker, snapshot.bestAsk, qty, tick))
                    orders.push_back(*o);
            }
        }
        return orders;
    }

    double pnl = (last - avgPrice) / avgPrice;

    if (pnl >= takeProfit || pnl <= -stopLoss) {
        if (snapshot.bestBid > 0.0) {
            int qty = std::min<int64_t>(pos.qty, maxOrderSize);
            if (qty > 0) {
                if (auto o = criarOrdemVenda(ticker, snapshot.bestBid, qty, tick))
                    orders.push_back(*o);
            }
        }
    }

    return orders;
}
