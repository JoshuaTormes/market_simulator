#pragma once
#include <fstream>
#include <string>
#include "Trade.h"

class TradeLogger {
public:
    explicit TradeLogger(const std::string& filename)
        : file(filename, std::ios::out) {
        file << "tick,ticker,price,qty,"
             << "buyOrderId,sellOrderId,"
             << "buyAgentId,buyAgentType,"
             << "sellAgentId,sellAgentType\n";
    }

    void write(
        uint64_t tick,
        const std::string& ticker,
        const Trade& trade,
        uint64_t buyAgentId,
        const std::string& buyType,
        uint64_t sellAgentId,
        const std::string& sellType
    ) {
        file << tick << ","
             << ticker << ","
             << trade.price << ","
             << trade.quantity << ","
             << trade.buyOrderId << ","
             << trade.sellOrderId << ","
             << buyAgentId << ","
             << buyType << ","
             << sellAgentId << ","
             << sellType << "\n";
    }

private:
    std::ofstream file;
};
