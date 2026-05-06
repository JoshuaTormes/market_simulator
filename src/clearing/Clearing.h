#pragma once
#include "ledger/PositionLedger.h"
#include "orderbook/Trade.h"
#include <string>
#include <utility>

// Bridges MatchingEngine output to the PositionLedger.
// One Clearing instance per instrument (ticker).
class Clearing {
public:
    Clearing(PositionLedger& ledger, std::string ticker);

    // Consume a trade: update both maker and taker positions.
    // Returns {maker_realized_pnl, taker_realized_pnl}.
    std::pair<Price, Price> apply(const Trade& trade);

    const std::string& ticker() const { return ticker_; }

private:
    PositionLedger& ledger_;
    std::string     ticker_;
};
