#pragma once
#include "IAgent.h"
#include <string>
#include <vector>
#include <optional>

class PositionLedger;

struct Position {
    int64_t qty = 0;
};

class AgentBase : public IAgent {
public:
    AgentBase(
        uint64_t id,
        double initialCash,
        int maxOrderSize,
        double maxDrawdownPct = 0.3,
        double maxPositionValuePct = 1.0
    );

    void bindLedger(const PositionLedger* ledger) override;
    uint64_t getId() const override;
    double getCash() const { return cash; }
    const std::vector<PnLRecord>& getPnLHistory() const override { return pnlHistory; }

    Position getPosition(const std::string& ticker) const;
    double getAvgEntryPrice(const std::string& ticker) const;
    void debitCash(double amount);
    void creditCash(double amount);

    std::optional<Order> criarOrdemCompra(
        const std::string& ticker,
        double price,
        int qty,
        uint64_t timestamp
    );

    std::optional<Order> criarOrdemVenda(
        const std::string& ticker,
        double price,
        int qty,
        uint64_t timestamp
    );

    void recordPnL(
        uint64_t tick,
        const std::unordered_map<std::string,double>& marketPrices
    ) override;

    double getPnL(
        const std::unordered_map<std::string,double>& marketPrices
    ) const;

protected:
    const PositionLedger* positionLedger = nullptr;

    bool riskOff() const;
    int adjustedOrderSize(double price) const;

    double cash;
    double initialCash;
    int maxOrderSize;
    double maxDrawdownPct;
    double maxPositionValuePct;

    std::vector<PnLRecord> pnlHistory;
};
