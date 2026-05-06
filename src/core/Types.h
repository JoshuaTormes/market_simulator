#pragma once
#include <cstdint>
#include <string>
#include <cassert>

// Discrete numeric types for price and quantity.
// All internal arithmetic uses integers to eliminate float drift.
// Convert to double only at UI/log boundaries.

using Price    = int64_t;   // price in ticks (divide by tick_size for double)
using Qty      = int64_t;   // quantity in lots
using Tick     = uint64_t;  // logical simulation tick
using SeqNo    = uint64_t;  // global monotonic sequence number (tie-breaking)
using AgentId  = uint64_t;
using OrderId  = uint64_t;

enum class Side { Buy, Sell };

inline Side opposite(Side s) {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// Instrument configuration; passed via SimulationConfig, stored globally per ticker.
struct InstrumentConfig {
    double tick_size  = 0.01;  // dollars per tick
    int64_t lot_size  = 1;     // shares per lot
    int64_t total_outstanding = 0; // total lots emitted at simulation start
};

// Conversion helpers — only call at UI/log/agent boundaries.
inline double to_price_double(Price p, double tick_size) {
    return static_cast<double>(p) * tick_size;
}

inline Price from_price_double(double price, double tick_size) {
    assert(tick_size > 0.0);
    return static_cast<Price>(price / tick_size + 0.5);
}

inline double to_qty_double(Qty q, int64_t lot_size) {
    return static_cast<double>(q) * static_cast<double>(lot_size);
}

inline Qty from_qty_double(double qty, int64_t lot_size) {
    assert(lot_size > 0);
    return static_cast<Qty>(qty / static_cast<double>(lot_size) + 0.5);
}
