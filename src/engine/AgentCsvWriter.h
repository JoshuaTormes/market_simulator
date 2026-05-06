#pragma once
#include <fstream>
#include <string>
#include <cstdint>

class AgentCsvWriter {
public:
    explicit AgentCsvWriter(const std::string& filename)
        : file(filename, std::ios::out | std::ios::trunc) {
        file << "agent_id,type\n";
        file.flush();
    }

    void write(uint64_t id, const std::string& type) {
        file << id << "," << type << "\n";
        file.flush();
    }

    ~AgentCsvWriter() {
        file.flush();
        file.close();
    }

private:
    std::ofstream file;
};
