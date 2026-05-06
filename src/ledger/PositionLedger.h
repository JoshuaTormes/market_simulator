#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>

struct PositionData {
    int64_t qty = 0;
    double cost = 0.0;
};

class PositionLedger {
public:
    void setInitialPosition(uint64_t agentId, const std::string& ticker, int64_t qty, double price = 0.0);

    void applyTrade(
        uint64_t buyAgentId,
        uint64_t sellAgentId,
        const std::string& ticker,
        int64_t qty,
        double price
    );

    int64_t getPosition(uint64_t agentId, const std::string& ticker) const;
    double getAvgPrice(uint64_t agentId, const std::string& ticker) const;

    const std::unordered_map<uint64_t, std::unordered_map<std::string, PositionData>>&
    getAllPositions() const;

    int64_t totalOutstanding(const std::string& ticker) const;

private:
    std::unordered_map<uint64_t, std::unordered_map<std::string, PositionData>> positions;
};
