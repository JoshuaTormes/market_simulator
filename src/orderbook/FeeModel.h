#pragma once
#include "core/Types.h"
#include <cmath>

// Fees are booked in fee units of 1/kFeeUnitsPerTick of a tick, so that a fee
// smaller than one tick per lot (0.1 bp of $100 is 0.1 tick) is not rounded
// away.  Trade::fee_*, PositionLedger::fees_net and the logged fee fields all
// carry this unit; divide by kFeeUnitsPerTick to get ticks·lots.
inline constexpr Price kFeeUnitsPerTick = 1000;

// Pluggable fee model: the taker pays a fraction of the notional, the maker
// optionally receives a rebate.  Rates in basis points of exec_price × qty.
// Both zero (the default) is a zero-fee market.
//
// No agent sees the fee: quotes, beliefs and risk limits are unchanged, so a
// fee changes the accounting of a run and nothing else.  That is what makes it
// a robustness test of who earns the surplus, not a model of fee competition.
struct FeeModel {
    double taker_fee_bp    = 0.0;   // charged to the taker
    double maker_rebate_bp = 0.0;   // credited to the maker (positive = rebate)

    static Price units(double bp, Qty qty, Price px) {
        return static_cast<Price>(std::llround(bp * 1e-4 * static_cast<double>(px)
                                               * static_cast<double>(qty)
                                               * static_cast<double>(kFeeUnitsPerTick)));
    }
    Price taker_fee(Qty qty, Price px)    const { return units(taker_fee_bp, qty, px); }
    Price maker_rebate(Qty qty, Price px) const { return units(maker_rebate_bp, qty, px); }
};
