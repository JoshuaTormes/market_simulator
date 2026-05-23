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

// ── Tag byte ─────────────────────────────────────────────────────────────────
enum class EventTag : uint8_t {
    FileHeader     = 0x00,
    Trade          = 0x01,
    MarketSnapshot = 0x02,
    NewsEvent      = 0x03,
    RegimeChange   = 0x04,
    LedgerEvent    = 0x05,
    // 0x06..0xFF reserved for future events
};

static constexpr uint32_t kFileMagic     = 0x534D4C42u;  // 'SMLB'
static constexpr uint16_t kSchemaVersion = 2;            // v2: ofi_tick replaces order_flow_imbalance field

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
    int8_t  regime;
    uint8_t _pad[7];
};
static_assert(sizeof(MarketSnapshotRecord) == 104, "MarketSnapshotRecord size changed");

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

#pragma pack(pop)

// ── Helper: payload size by tag ───────────────────────────────────────────────
inline size_t payload_size(EventTag tag) {
    switch (tag) {
        case EventTag::FileHeader:     return sizeof(FileHeaderRecord);
        case EventTag::Trade:          return sizeof(TradeRecord);
        case EventTag::MarketSnapshot: return sizeof(MarketSnapshotRecord);
        case EventTag::NewsEvent:      return sizeof(NewsEventRecord);
        case EventTag::RegimeChange:   return sizeof(RegimeChangeRecord);
        case EventTag::LedgerEvent:    return sizeof(LedgerEventRecord);
        default:                       return 0;
    }
}
