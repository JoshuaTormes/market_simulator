#pragma once
// In-memory event → packed on-disk record.  Shared by BinaryLogWriter and by
// SeriesCollector, so the report computed from memory and the one computed
// from a .bin read back see bit-identical inputs.
#include "EventSchema.h"
#include "orderbook/Trade.h"
#include "marketdata/MarketSnapshot.h"
#include "agents/MetaorderEvent.h"

inline TradeRecord to_record(const Trade& t) {
    TradeRecord r{};
    r.price       = t.price;
    r.qty         = t.qty;
    r.maker_id    = t.maker_id;
    r.taker_id    = t.taker_id;
    r.taker_side  = (t.taker_side == Side::Buy) ? 0 : 1;
    r.maker_agent = t.maker_agent;
    r.taker_agent = t.taker_agent;
    r.tick        = t.tick;
    r.seq_no      = t.seq_no;
    r.fee_maker   = t.fee_maker;
    r.fee_taker   = t.fee_taker;
    return r;
}

inline MarketSnapshotRecord to_record(const MarketSnapshot& s) {
    MarketSnapshotRecord r{};
    r.tick                  = s.tick;
    r.mid_price             = s.mid_price;
    r.spread                = s.spread;
    r.last_trade_price      = s.last_trade_price;
    r.realized_vol_s        = s.realized_vol[0];
    r.realized_vol_m        = s.realized_vol[1];
    r.realized_vol_l        = s.realized_vol[2];
    r.vwap_s                = s.vwap[0];
    r.ofi_tick              = s.ofi_tick;
    r.trade_imbalance       = s.trade_imbalance;
    r.momentum              = s.momentum;
    r.book_imbalance_l1     = s.book_imbalance[0];
    r.fundamental_value     = s.fundamental_value;
    r.news_cum_log          = s.news_cum_log;
    r.regime                = static_cast<int8_t>(s.regime);
    return r;
}

inline MetaorderRecord to_record(const MetaorderEvent& m) {
    MetaorderRecord r{};
    r.tick_start    = m.tick_start;
    r.tick_end      = m.tick_end;
    r.agent_id      = m.agent;
    r.side          = (m.side == Side::Buy) ? 0 : 1;
    r.qty_requested = m.qty_requested;
    r.qty_submitted = m.qty_submitted;
    r.qty_executed  = m.qty_executed;
    return r;
}
