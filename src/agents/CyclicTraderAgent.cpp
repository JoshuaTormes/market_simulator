#include "CyclicTraderAgent.h"
#include <algorithm>


CyclicTraderAgent::CyclicTraderAgent(
    uint64_t id,
    double cash,
    int maxSize,
    int tickInterval,
    bool startBuying
)
    : AgentBase(id, cash, maxSize),
      tickInterval(tickInterval),
      buyNext(startBuying) {}

std::vector<Order> CyclicTraderAgent::analisar(
    const MarketSnapshot& snapshot,
    uint64_t tick,
    const std::string& ticker
) {
    std::vector<Order> orders;

    if (tick < lastTradeTick + tickInterval)
        return orders;

    Position pos = getPosition(ticker);

    if (buyNext) {
        int qty = std::min(maxOrderSize, static_cast<int>(cash / snapshot.bestAsk));
        if (qty > 0) {
            if (auto o = criarOrdemCompra(ticker, snapshot.bestAsk, qty, tick))
                orders.push_back(*o);
        }
    } else {
        int qty = std::min(maxOrderSize, static_cast<int>(pos.qty));
        if (qty > 0) {
            if (auto o = criarOrdemVenda(ticker, snapshot.bestBid, qty, tick))
                orders.push_back(*o);
        }
    }

    buyNext = !buyNext;  
    lastTradeTick = tick;

    return orders;
}
