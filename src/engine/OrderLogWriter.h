#pragma once
#include <fstream>
#include <string>
#include "../orderbook/Order.h"

class OrderLogWriter {
public:
    explicit OrderLogWriter(const std::string& path);
    void write(uint64_t tick, const Order& order, const std::string& agentType);

private:
    std::ofstream file;
    bool headerWritten;
};
