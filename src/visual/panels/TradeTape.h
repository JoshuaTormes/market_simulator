#pragma once
// Scrolling trade tape: last N trades coloured by taker side.
#include "marketdata/TradeTapeBuffer.h"

class TradeTape {
public:
    explicit TradeTape(TradeTapeBuffer& buf) : buf_(buf) {}
    void draw();

private:
    TradeTapeBuffer& buf_;
};
