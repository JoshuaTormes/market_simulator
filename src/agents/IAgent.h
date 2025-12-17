#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../orderbook/Order.h"
#include "../orderbook/MarketSnapshot.h"

struct PnLRecord {
    uint64_t tick;
    double pnl;
};

class IAgent {
public:
    explicit IAgent(uint64_t id) : id(id) {}
    virtual ~IAgent() = default;

    virtual uint64_t getId() const { return id; }
    virtual std::optional<Order> analisar(const MarketSnapshot& snapshot, uint64_t tick, const std::string& ticker) = 0;
    virtual const char* type() const = 0;

    virtual void recordPnL(uint64_t tick, const std::unordered_map<std::string,double>& marketPrices) {}
    virtual const std::vector<PnLRecord>& getPnLHistory() const {
        static std::vector<PnLRecord> empty;
        return empty;
    }

protected:
    uint64_t id;
};
