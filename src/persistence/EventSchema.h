#pragma once
// Binary log schema. All payload structs are tightly-packed PODs — no heap
// allocation, no pointers, no padding.  Layout is pinned at schema version 1;
// bump kSchemaVersion and add migration logic before changing any struct.
//
// On-disk record format:
//   [1 byte EventTag][N bytes payload]   (N is compile-time constant per tag)
//
// File starts with a FileHeaderRecord (tag FileHeader) that contains the
// magic number, schema version, seed and run configuration.
#include "core/Types.h"
#include <cstdint>
#include <cstring>
#include <limits>

// ── Tag byte ─────────────────────────────────────────────────────────────────
enum class EventTag : uint8_t {
    FileHeader     = 0x00,
    Trade          = 0x01,
    MarketSnapshot = 0x02,
    NewsEvent      = 0x03,
    RegimeChange   = 0x04,
    LedgerEvent    = 0x05,
    Metaorder      = 0x06,   // v4: one record per completed institutional parent
    // 0x07..0xFF reserved for future events
};

static constexpr uint32_t kFileMagic     = 0x534D4C42u;  // 'SMLB'
static constexpr uint16_t kSchemaVersion = 4;            // v4: snapshot gains news_cum_log; Metaorder records
static constexpr uint16_t kMinReadableVersion = 1;       // v1/v2 logs stay readable (fundamental_value = 0)

// ── Packed payload records ────────────────────────────────────────────────────
#pragma pack(push, 1)

struct FileHeaderRecord {
    uint32_t magic;          // kFileMagic
    uint16_t version;        // kSchemaVersion
    uint16_t _pad0;
    uint64_t seed;
    uint64_t max_ticks;
    uint64_t publish_interval;
    uint8_t  ticker[16];     // null-terminated, truncated to 15 chars
    uint8_t  _pad1[8];
};
static_assert(sizeof(FileHeaderRecord) == 56, "FileHeaderRecord size changed");

struct TradeRecord {
    Price   price;           // maker price in ticks
    Qty     qty;
    OrderId maker_id;
    OrderId taker_id;
    uint8_t taker_side;      // 0=Buy, 1=Sell
    uint8_t _pad[7];
    AgentId maker_agent;
    AgentId taker_agent;
    Tick    tick;
    SeqNo   seq_no;
    Price   fee_maker;
    Price   fee_taker;
};
static_assert(sizeof(TradeRecord) == 88, "TradeRecord size changed");

// v1/v2 snapshot layout — kept verbatim so old logs stay readable.  v2 only
// changed the meaning of the ofi field, not the bytes, so one struct covers
// both.  Do not modify.
struct MarketSnapshotRecordV2 {
    Tick    tick;
    Price   mid_price;
    Price   spread;
    Price   last_trade_price;
    double  realized_vol_s;
    double  realized_vol_m;
    double  realized_vol_l;
    double  vwap_s;
    double  ofi_tick;
    double  trade_imbalance;
    double  momentum;
    double  book_imbalance_l1;
    int8_t  regime;
    uint8_t _pad[7];
};
static_assert(sizeof(MarketSnapshotRecordV2) == 104, "MarketSnapshotRecordV2 is frozen");

// v3 snapshot layout — frozen.  Do not modify.
struct MarketSnapshotRecordV3 {
    Tick    tick;
    Price   mid_price;
    Price   spread;
    Price   last_trade_price;
    double  realized_vol_s;   // short window
    double  realized_vol_m;   // medium window
    double  realized_vol_l;   // long window
    double  vwap_s;
    double  ofi_tick;         // per-tick OFI: Σbuy_vol − Σsell_vol (v2: was windowed OFI)
    double  trade_imbalance;
    double  momentum;
    double  book_imbalance_l1;
    double  fundamental_value; // v3: latent V in tick-units (0.0 in v2 logs)
    int8_t  regime;
    uint8_t _pad[7];
};
static_assert(sizeof(MarketSnapshotRecordV3) == 112, "MarketSnapshotRecordV3 is frozen");

struct MarketSnapshotRecord {
    Tick    tick;
    Price   mid_price;
    Price   spread;
    Price   last_trade_price;
    double  realized_vol_s;   // short window
    double  realized_vol_m;   // medium window
    double  realized_vol_l;   // long window
    double  vwap_s;
    double  ofi_tick;         // per-tick OFI: Σbuy_vol − Σsell_vol (v2: was windowed OFI)
    double  trade_imbalance;
    double  momentum;
    double  book_imbalance_l1;
    double  fundamental_value; // v3: latent V in tick-units (0.0 in v2 logs)
    // v4: cumulative log-return of every announcement applied to V so far.
    // Splits log V into its news component and the diffusion, which is what
    // lets price discovery be measured on the part makers are not told about.
    // NaN in logs older than v4 (unknown, not zero).
    double  news_cum_log;
    int8_t  regime;
    uint8_t _pad[7];
};
static_assert(sizeof(MarketSnapshotRecord) == 120, "MarketSnapshotRecord size changed");

// Widen a v2 record into the current layout (fundamental_value unknown → 0).
inline MarketSnapshotRecord upgrade_v2(const MarketSnapshotRecordV2& o) {
    MarketSnapshotRecord r{};
    r.tick = o.tick;                     r.mid_price = o.mid_price;
    r.spread = o.spread;                 r.last_trade_price = o.last_trade_price;
    r.realized_vol_s = o.realized_vol_s; r.realized_vol_m = o.realized_vol_m;
    r.realized_vol_l = o.realized_vol_l; r.vwap_s = o.vwap_s;
    r.ofi_tick = o.ofi_tick;             r.trade_imbalance = o.trade_imbalance;
    r.momentum = o.momentum;             r.book_imbalance_l1 = o.book_imbalance_l1;
    r.fundamental_value = 0.0;           r.regime = o.regime;
    r.news_cum_log = std::numeric_limits<double>::quiet_NaN();
    return r;
}

// Widen a v3 record (news_cum_log unknown → NaN).
inline MarketSnapshotRecord upgrade_v3(const MarketSnapshotRecordV3& o) {
    MarketSnapshotRecord r{};
    r.tick = o.tick;                     r.mid_price = o.mid_price;
    r.spread = o.spread;                 r.last_trade_price = o.last_trade_price;
    r.realized_vol_s = o.realized_vol_s; r.realized_vol_m = o.realized_vol_m;
    r.realized_vol_l = o.realized_vol_l; r.vwap_s = o.vwap_s;
    r.ofi_tick = o.ofi_tick;             r.trade_imbalance = o.trade_imbalance;
    r.momentum = o.momentum;             r.book_imbalance_l1 = o.book_imbalance_l1;
    r.fundamental_value = o.fundamental_value;
    r.news_cum_log = std::numeric_limits<double>::quiet_NaN();
    r.regime = o.regime;
    return r;
}

struct NewsEventRecord {
    Tick    announce_tick;
    double  impact_log_return;
    double  duration_ticks;
    double  dispersion_sigma;
    uint8_t ticker[16];
};
static_assert(sizeof(NewsEventRecord) == 48, "NewsEventRecord size changed");

struct RegimeChangeRecord {
    Tick    tick;
    int8_t  old_regime;
    int8_t  new_regime;
    uint8_t _pad[6];
};
static_assert(sizeof(RegimeChangeRecord) == 16, "RegimeChangeRecord size changed");

struct LedgerEventRecord {
    Tick    tick;
    AgentId agent_id;
    uint8_t side;            // 0=Buy, 1=Sell
    uint8_t _pad[7];
    Price   exec_price;
    Qty     qty;
    Price   fee;
};
static_assert(sizeof(LedgerEventRecord) == 48, "LedgerEventRecord size changed");

// v4: one completed parent order of an institutional desk.  Impact of a
// metaorder is measured from these against the mid path, so the regression is
// on an exogenous quantity (the parent size) rather than on the per-tick OFI
// the makers themselves price from.
struct MetaorderRecord {
    Tick    tick_start;      // tick the parent arrived (first child submitted)
    Tick    tick_end;        // tick the last child was submitted
    AgentId agent_id;
    uint8_t side;            // 0=Buy, 1=Sell
    uint8_t _pad[7];
    Qty     qty_requested;   // parent size
    Qty     qty_submitted;   // Σ child sizes sent (residual may be dropped)
    Qty     qty_executed;    // filled, from the desk's own inventory change
};
static_assert(sizeof(MetaorderRecord) == 56, "MetaorderRecord size changed");

#pragma pack(pop)

// ── Helper: payload size by tag ───────────────────────────────────────────────
// Snapshot size depends on the file's schema version; the rest are fixed.
inline size_t payload_size(EventTag tag, uint16_t version = kSchemaVersion) {
    switch (tag) {
        case EventTag::FileHeader:     return sizeof(FileHeaderRecord);
        case EventTag::Trade:          return sizeof(TradeRecord);
        case EventTag::MarketSnapshot:
            if (version < 3)  return sizeof(MarketSnapshotRecordV2);
            if (version == 3) return sizeof(MarketSnapshotRecordV3);
            return sizeof(MarketSnapshotRecord);
        case EventTag::NewsEvent:      return sizeof(NewsEventRecord);
        case EventTag::RegimeChange:   return sizeof(RegimeChangeRecord);
        case EventTag::LedgerEvent:    return sizeof(LedgerEventRecord);
        case EventTag::Metaorder:      return version >= 4 ? sizeof(MetaorderRecord) : 0;
        default:                       return 0;
    }
}
