#include "OrderLogWriter.h"

OrderLogWriter::OrderLogWriter(const std::string& path)
    : file(path) {
    file << "tick,agent_id,side,price,qty\n";
}

void OrderLogWriter::write(uint64_t tick, const Order& order) {
    file << tick << ","
         << order.agentId << ","
         << (order.side == Side::Buy ? "BUY" : "SELL") << ","
         << order.price << ","
         << order.quantity << "\n";
}
