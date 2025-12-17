#pragma once
#include <fstream>
#include <string>
#include "Candle.h"

class CandleCsvWriter {
public:
    explicit CandleCsvWriter(const std::string& filename);
    void write(const Candle& candle);

private:
    std::ofstream file;
};
