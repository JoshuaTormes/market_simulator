#include "PositionCsvWriter.h"

PositionCsvWriter::PositionCsvWriter(const std::string& path)
    : file(path), headerWritten(false) {}

void PositionCsvWriter::write(
    uint64_t tick,
    uint64_t agentId,
    const std::string& agentType,
    const std::string& ticker,
    double cash,
    int64_t position
) {
    if (!headerWritten) {
        file << "tick,agentId,agentType,ticker,position\n";
        headerWritten = true;
    }

    file << tick << ","
         << agentId << ","
         << agentType << ","
         << ticker << ","
         << cash << ","
         << position << "\n";
}
