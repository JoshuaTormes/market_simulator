#pragma once
#include "core/Types.h"

// Per-agent risk budget. Enforced by RiskGate before any action reaches the book.
struct RiskLimits {
    Qty    max_position       = 1000;           // max |net_qty| in lots
    Price  max_notional       = 100'000'000LL;  // price-ticks × lots
    // Loss limit: an order that would add risk is rejected once the agent's
    // cumulative MtM P&L (realized + unrealized, since the start of the run)
    // falls below −max_drawdown_pct·max_notional.  It is not measured from a
    // peak and never resets, so it is an absorbing barrier: an agent that
    // crosses it can only reduce its position from then on.  Disabled (1.0) by
    // default — at 0.20 it silently took the most active maker out of the
    // ecology between sessions 39 and 67, which the population never chose.
    double max_drawdown_pct   = 1.0;
    double max_leverage       = 10.0;           // notional / equity
    double margin_req_pct     = 0.0;            // fraction of notional held as margin (0 = disabled)
    double borrow_fee_per_tick = 0.0;           // daily-rate / ticks_per_day for short positions
};
