#include "AgentBase.h"

AgentBase::AgentBase(uint64_t id, double initialCash, int maxOrderSize)
    : IAgent(id), cash(initialCash), maxOrderSize(maxOrderSize) {}

uint64_t AgentBase::getId() const { return id; }

Position AgentBase::getPosition(const std::string& ticker) const {
    auto it = positions.find(ticker);
    if (it != positions.end()) return it->second;
    return Position{};
}

void AgentBase::executeTrade(const std::string& ticker, int64_t qty, double price) {
    Position& pos = positions[ticker];
    double totalCost = pos.avgPrice * pos.qty + price * qty;
    pos.qty += qty;
    pos.avgPrice = (pos.qty != 0) ? totalCost / pos.qty : 0;
    cash -= qty * price;
}

std::optional<Order> AgentBase::criarOrdemCompra(const std::string& ticker, double price, int qty, uint64_t timestamp) {
    if (qty <= 0 || price * qty > cash) return std::nullopt;
    atualizarCarteiraCompra(ticker, price, qty);
    cash -= price * qty;

    Order o{};
    o.agentId = id;
    o.side = Side::Buy;
    o.price = price;
    o.quantity = qty;
    o.timestamp = timestamp;
    o.ticker = ticker;
    return o;
}

std::optional<Order> AgentBase::criarOrdemVenda(const std::string& ticker, double price, int qty, uint64_t timestamp) {
    auto it = positions.find(ticker);
    if (it == positions.end() || it->second.qty < qty || qty <= 0) return std::nullopt;
    atualizarCarteiraVenda(ticker, qty);
    cash += price * qty;

    Order o{};
    o.agentId = id;
    o.side = Side::Sell;
    o.price = price;
    o.quantity = qty;
    o.timestamp = timestamp;
    o.ticker = ticker;
    return o;
}

void AgentBase::atualizarCarteiraCompra(const std::string& ticker, double price, int qty) {
    Position& pos = positions[ticker];
    pos.avgPrice = ((pos.avgPrice * pos.qty) + (price * qty)) / (pos.qty + qty);
    pos.qty += qty;
}

void AgentBase::atualizarCarteiraVenda(const std::string& ticker, int qty) {
    Position& pos = positions[ticker];
    pos.qty -= qty;
    if (pos.qty == 0) pos.avgPrice = 0.0;
}

double AgentBase::getPnL(const std::unordered_map<std::string,double>& marketPrices) const {
    double value = cash;
    for (const auto& [ticker,pos] : positions) {
        auto it = marketPrices.find(ticker);
        if (it != marketPrices.end()) value += pos.qty * it->second;
    }
    return value;
}

void AgentBase::recordPnL(uint64_t tick, const std::unordered_map<std::string,double>& marketPrices) {
    pnlHistory.push_back({tick, getPnL(marketPrices)});
}
