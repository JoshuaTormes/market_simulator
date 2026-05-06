#pragma once
#include "core/Types.h"

// Pluggable fee model: taker pays, maker optionally receives rebate.
// All values are in ticks (same unit as Price).
// Set both to 0 for a zero-fee market.
struct FeeModel {
    Price taker_fee_per_lot  = 0;  // charged to the taker per lot traded
    Price maker_rebate_per_lot = 0; // credited to the maker per lot traded (positive = rebate)

    Price taker_fee(Qty qty)   const { return taker_fee_per_lot  * qty; }
    Price maker_rebate(Qty qty) const { return maker_rebate_per_lot * qty; }
};
