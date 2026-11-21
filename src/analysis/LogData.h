#pragma once
#include "core/Types.h"
#include <cstdint>
#include <map>
#include <vector>

// Everything the analysis reads from a run, independent of where it came from.
// `Report::extract` fills it from a binary log; a collector running inside the
// simulation loop can fill it directly, so long runs need not touch the disk.
struct LogData {
    std::vector<double>   mid_prices;
    std::vector<uint64_t> price_ticks;
    std::vector<double>   spreads;
    std::vector<double>   ofi;
    std::vector<double>   fundamental;       // latent V in ticks (empty on v2 logs)
    std::vector<double>   trade_signs;       // +1 = buy-aggressor, -1 = sell-aggressor
    std::vector<double>   volume_per_snap;   // total trade volume since last snapshot
    size_t                n_trades    = 0;
    size_t                n_snapshots = 0;
    size_t                n_two_sided = 0;   // snapshots with spread > 0
    double                mm_volume   = 0.0; // volume with MM maker and MM taker
    std::map<AgentId, double> volume_by_mm;  // maker-side + taker-side volume per MM
    double                total_volume = 0.0;
    bool                  has_fundamental = false;
    // Live runs only (not in the log): Σ and count of the non-maker share of
    // the touch over two-sided snapshots.
    double                non_mm_top_sum = 0.0;
    size_t                non_mm_top_n   = 0;
    // v4: Σ announced log-return applied to V, aligned with `fundamental`.
    // Empty unless every kept snapshot carries it (v3 and older logs: empty).
    std::vector<double>   news_cum;
    bool                  has_news_cum = false;
    // v4: completed institutional parents, in completion order.
    struct Metaorder {
        uint64_t tick_start = 0, tick_end = 0;
        AgentId  agent = 0;
        int      sign  = 1;          // +1 buy, -1 sell
        double   qty_requested = 0.0, qty_submitted = 0.0, qty_executed = 0.0;
    };
    std::vector<Metaorder> metaorders;
};
