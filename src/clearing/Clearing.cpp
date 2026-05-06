#include "Clearing.h"

Clearing::Clearing(PositionLedger& ledger, std::string ticker)
    : ledger_(ledger), ticker_(std::move(ticker)) {}

std::pair<Price, Price> Clearing::apply(const Trade& trade) {
    // The taker's side is stored directly on the trade.
    // The maker's side is the opposite.
    Side taker_side = trade.taker_side;
    Side maker_side = (taker_side == Side::Buy) ? Side::Sell : Side::Buy;

    // fee_maker is stored as a negative number in Trade (rebate credit).
    // Pass the raw value to PositionLedger; fees_net will go negative for rebates.
    Price maker_pnl = ledger_.apply(trade.maker_agent, maker_side, ticker_,
                                     trade.qty, trade.price, trade.fee_maker);
    Price taker_pnl = ledger_.apply(trade.taker_agent, taker_side, ticker_,
                                     trade.qty, trade.price, trade.fee_taker);
    return {maker_pnl, taker_pnl};
}
