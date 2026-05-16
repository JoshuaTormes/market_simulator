#pragma once
// Offline CSV export.  Reads a .bin log and writes:
//   out_dir/trades.csv      — one row per TradeRecord
//   out_dir/snapshots.csv   — one row per MarketSnapshotRecord
//   out_dir/news.csv        — one row per NewsEventRecord
//   out_dir/regimes.csv     — one row per RegimeChangeRecord
// Not used in the hot path; intended for post-run analysis.
#include <string>

class CsvExporter {
public:
    struct Stats {
        uint64_t trades;
        uint64_t snapshots;
        uint64_t news_events;
        uint64_t regime_changes;
        uint64_t total_bytes_in;
    };

    static Stats export_all(const std::string& bin_path,
                            const std::string& out_dir);
};
