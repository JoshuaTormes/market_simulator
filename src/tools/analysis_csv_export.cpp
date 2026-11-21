// CLI: analysis_csv_export <log.bin> <out_dir>
// Reads a binary simulation log and writes trades.csv, snapshots.csv,
// news.csv, regimes.csv to <out_dir>.
#include "persistence/CsvExporter.h"
#include "persistence/Replay.h"
#include <cstdio>
#include <string>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: analysis_csv_export <log.bin> <out_dir>\n");
        return 1;
    }

    std::string bin_path = argv[1];
    std::string out_dir  = argv[2];

    // Print log summary first.
    auto ls = Replay::count_records(bin_path);
    std::fprintf(stdout,
        "Log summary — trades:%llu  snapshots:%llu  news:%llu  "
        "regime_changes:%llu  total:%llu\n",
        (unsigned long long)ls.trades,
        (unsigned long long)ls.snapshots,
        (unsigned long long)ls.news_events,
        (unsigned long long)ls.regime_changes,
        (unsigned long long)ls.total_records);

    std::filesystem::create_directories(out_dir);

    try {
        auto stats = CsvExporter::export_all(bin_path, out_dir);
        std::fprintf(stdout,
            "Exported to %s — trades:%llu  snapshots:%llu  "
            "news:%llu  regime_changes:%llu  input:%.1f KB\n",
            out_dir.c_str(),
            (unsigned long long)stats.trades,
            (unsigned long long)stats.snapshots,
            (unsigned long long)stats.news_events,
            (unsigned long long)stats.regime_changes,
            stats.total_bytes_in / 1024.0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
