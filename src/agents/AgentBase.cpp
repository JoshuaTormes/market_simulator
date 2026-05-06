#include "AgentBase.h"
#include "../ledger/PositionLedger.h"
#include <algorithm>
#include <iostream>

AgentBase::AgentBase(
    uint64_t id,
    double initialCash,
    int maxOrderSize,
    double maxDrawdownPct,
    double maxPositionValuePct
)
    : IAgent(id),
      cash(initialCash),
      initialCash(initialCash),
      maxOrderSize(maxOrderSize),
      maxDrawdownPct(maxDrawdownPct),
      maxPositionValuePct(maxPositionValuePct) {}

uint64_t AgentBase::getId() const {
    return id;
}

void AgentBase::bindLedger(const PositionLedger* ledger) {
    positionLedger = ledger;
}

Position AgentBase::getPosition(const std::string& ticker) const {
    if (!positionLedger) return Position{};
    return Position{ positionLedger->getPosition(id, ticker) };
}

bool AgentBase::riskOff() const {
    if (pnlHistory.empty()) return false;
    double pnl = pnlHistory.back().pnl;
    return pnl < initialCash * (1.0 - maxDrawdownPct);
}

int AgentBase::adjustedOrderSize(double price) const {
    double maxValue = initialCash * maxPositionValuePct;
    int maxQtyByRisk = static_cast<int>(maxValue / price);
    return std::min(maxOrderSize, maxQtyByRisk);
}

std::optional<Order> AgentBase::criarOrdemCompra(
    const std::string& ticker,
    double price,
    int qty,
    uint64_t timestamp
) {
    // if (riskOff()) return std::nullopt;

    int adjQty = std::min(qty, adjustedOrderSize(price));
    if (adjQty <= 0) return std::nullopt;

    Order o{};
    o.agentId = id;
    o.side = Side::Buy;
    o.price = price;
    o.quantity = adjQty;
    o.timestamp = timestamp;
    o.ticker = ticker;
    return o;
}

std::optional<Order> AgentBase::criarOrdemVenda(
    const std::string& ticker,
    double price,
    int qty,
    uint64_t timestamp
) {
    if (!positionLedger) {
        // std::cout << "Sem ledger, não pode vender\n";
        return std::nullopt;
    }

    int64_t posQty = positionLedger->getPosition(id, ticker);
    // std::cout << "Agent " << id << " tentando vender " << qty
    //           << " ações de " << ticker << ", posição atual: " << posQty << "\n";

    int adjQty = std::min<int64_t>(qty, posQty);
    if (adjQty <= 0) {
        // std::cout << "Quantidade ajustada <= 0, não vende\n";
        return std::nullopt;
    }

    // std::cout << "Criando ordem de venda para " << adjQty << " ações a " << price << "\n";

    Order o{};
    o.agentId = id;
    o.side = Side::Sell;
    o.price = price;
    o.quantity = adjQty;
    o.timestamp = timestamp;
    o.ticker = ticker;
    return o;
}


double AgentBase::getPnL(
    const std::unordered_map<std::string,double>& marketPrices
) const {
    double value = cash;

    if (!positionLedger) return value;

    for (const auto& [ticker, price] : marketPrices) {
        int64_t qty = positionLedger->getPosition(id, ticker);
        value += qty * price;
    }

    return value;
}

void AgentBase::recordPnL(
    uint64_t tick,
    const std::unordered_map<std::string,double>& marketPrices
) {
    pnlHistory.push_back({tick, getPnL(marketPrices)});
}

double AgentBase::getAvgEntryPrice(const std::string& ticker) const {
    if (!positionLedger)
        return 0.0;
    return positionLedger->getAvgPrice(id, ticker);
}


void AgentBase::debitCash(double amount) {
    cash -= amount;
}

void AgentBase::creditCash(double amount) {
    cash += amount;
}
