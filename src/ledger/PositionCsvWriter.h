#pragma once
#include <fstream>
#include <string>
#include <cstdint>

class PositionCsvWriter {
public:
    explicit PositionCsvWriter(const std::string& path);
    void write(
        uint64_t tick,
        uint64_t agentId,
        const std::string& agentType,
        const std::string& ticker,
        double cash,
        int64_t position
    );

private:
    std::ofstream file;
    bool headerWritten;
};
