#pragma once
#include "IAgent.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

struct Position {
    int64_t qty = 0;
    double avgPrice = 0;
};

class AgentBase : public IAgent {
public:
    AgentBase(uint64_t id, double initialCash, int maxOrderSize);

    uint64_t getId() const override;
    double getCash() const { return cash; }
    const std::vector<PnLRecord>& getPnLHistory() const override { return pnlHistory; }
    Position getPosition(const std::string& ticker) const;
    const std::unordered_map<std::string,Position>& getPortfolio() const { return positions; }

    void executeTrade(const std::string& ticker, int64_t qty, double price);

    std::optional<Order> criarOrdemCompra(const std::string& ticker, double price, int qty, uint64_t timestamp);
    std::optional<Order> criarOrdemVenda(const std::string& ticker, double price, int qty, uint64_t timestamp);

    void recordPnL(uint64_t tick, const std::unordered_map<std::string,double>& marketPrices) override;
    double getPnL(const std::unordered_map<std::string,double>& marketPrices) const;

protected:
    void atualizarCarteiraCompra(const std::string& ticker, double price, int qty);
    void atualizarCarteiraVenda(const std::string& ticker, int qty);

    double cash;
    int maxOrderSize;
    std::unordered_map<std::string, Position> positions;
    std::vector<PnLRecord> pnlHistory;
};
