#include "PositionLedger.h"

void PositionLedger::setInitialPosition(
    uint64_t agentId,
    const std::string& ticker,
    int64_t qty,
    double price
) {
    auto& pos = positions[agentId][ticker];
    pos.qty += qty;
    pos.cost += price * qty;
}

void PositionLedger::applyTrade(
    uint64_t buyAgentId,
    uint64_t sellAgentId,
    const std::string& ticker,
    int64_t qty,
    double price
) {
    auto& buyPos = positions[buyAgentId][ticker];
    buyPos.qty += qty;
    buyPos.cost += price * qty;

    auto& sellPos = positions[sellAgentId][ticker];
    if (sellPos.qty > 0) {
        double avg = sellPos.cost / sellPos.qty;
        sellPos.qty -= qty;
        sellPos.cost -= avg * qty;
        if (sellPos.qty == 0)
            sellPos.cost = 0.0;
    }
}

int64_t PositionLedger::getPosition(uint64_t agentId, const std::string& ticker) const {
    auto itAgent = positions.find(agentId);
    if (itAgent == positions.end()) return 0;

    auto itTicker = itAgent->second.find(ticker);
    if (itTicker == itAgent->second.end()) return 0;

    return itTicker->second.qty;
}

double PositionLedger::getAvgPrice(uint64_t agentId, const std::string& ticker) const {
    auto itAgent = positions.find(agentId);
    if (itAgent == positions.end()) return 0.0;

    auto itTicker = itAgent->second.find(ticker);
    if (itTicker == itAgent->second.end()) return 0.0;

    const auto& pos = itTicker->second;
    if (pos.qty == 0) return 0.0;

    return pos.cost / pos.qty;
}

const std::unordered_map<uint64_t, std::unordered_map<std::string, PositionData>>&
PositionLedger::getAllPositions() const {
    return positions;
}

int64_t PositionLedger::totalOutstanding(const std::string& ticker) const {
    int64_t total = 0;
    for (const auto& [agentId, posByTicker] : positions) {
        auto it = posByTicker.find(ticker);
        if (it != posByTicker.end())
            total += it->second.qty;
    }
    return total;
}
