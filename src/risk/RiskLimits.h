#pragma once
#include "core/Types.h"

// Per-agent risk budget. Enforced by RiskGate before any action reaches the book.
struct RiskLimits {
    Qty    max_position       = 1000;           // max |net_qty| in lots
    Price  max_notional       = 100'000'000LL;  // price-ticks × lots
    double max_drawdown_pct   = 0.20;           // fraction of peak equity
    double max_leverage       = 10.0;           // notional / equity
    double margin_req_pct     = 0.10;           // fraction of notional held as margin
    double borrow_fee_per_tick = 0.0;           // daily-rate / ticks_per_day for short positions
};
