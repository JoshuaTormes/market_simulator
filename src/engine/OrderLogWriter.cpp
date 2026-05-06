#include "OrderLogWriter.h"

OrderLogWriter::OrderLogWriter(const std::string& path)
    : file(path), headerWritten(false) {}

void OrderLogWriter::write(uint64_t tick, const Order& order, const std::string& agentType) {
    if (!headerWritten) {
        file << "tick,agent_id,agent_type,side,order_type,price,qty\n";
        headerWritten = true;
    }

    file << tick << ","
         << order.agentId << ","
         << agentType << ","
         << (order.side == Side::Buy ? "BUY" : "SELL") << ","
         << (order.type == OrderType::Limit ? "LIMIT" : "MARKET") << ","
         << order.price << ","
         << order.quantity << "\n";
}
