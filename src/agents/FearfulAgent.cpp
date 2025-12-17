#include "FearfulAgent.h"

FearfulAgent::FearfulAgent(uint64_t id, double cash, int maxSize, double sl, double tp)
    : AgentBase(id, cash, maxSize), stopLossPct(sl), takeProfitPct(tp) {}

std::optional<Order> FearfulAgent::analisar(const MarketSnapshot& snapshot, uint64_t timestamp, const std::string& ticker) {
    Position pos = getPosition(ticker);
    double entryPrice = entryPrices[ticker];

    if (pos.qty > 0) {
        double ret = (snapshot.lastPrice - entryPrice) / entryPrice;

        if (ret <= -stopLossPct || ret >= takeProfitPct) {
            entryPrices[ticker] = 0.0;
            return criarOrdemVenda(ticker, snapshot.lastPrice, pos.qty, timestamp);
        }
        return std::nullopt;
    }

    int qty = maxOrderSize;
    auto ordem = criarOrdemCompra(ticker, snapshot.lastPrice, qty, timestamp); 
    if (ordem) entryPrices[ticker] = snapshot.lastPrice;

    return ordem;
}
