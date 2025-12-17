#pragma once
#include <fstream>
#include <string>

class AgentCsvWriter {
public:
    explicit AgentCsvWriter(const std::string& filename)
        : file(filename) {
        file << "agent_id,type\n";
    }

    void write(uint64_t id, const std::string& type) {
        file << id << "," << type << "\n";
    }

private:
    std::ofstream file;
};
