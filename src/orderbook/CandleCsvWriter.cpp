#include "CandleCsvWriter.h"

CandleCsvWriter::CandleCsvWriter(const std::string& filename)
    : file(filename) {
    file << "index,startTick,open,high,low,close,volume\n";
}

void CandleCsvWriter::write(const Candle& candle) {
    file << candle.index << ","
         << candle.startTick << ","
         << candle.open << ","
         << candle.high << ","
         << candle.low << ","
         << candle.close << ","
         << candle.volume << "\n";
}