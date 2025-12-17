#pragma once
#include <fstream>
#include "../orderbook/Order.h"

class OrderLogWriter {
public:
    explicit OrderLogWriter(const std::string& path);
    void write(uint64_t tick, const Order& order);

private:
    std::ofstream file;
};
