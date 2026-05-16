#include "Replay.h"
#include <cstdio>
#include <cstring>

bool Replay::load_config(const std::string& bin_path, SimulationConfig& out_cfg) {
    try {
        BinaryLogReader reader(bin_path);
        FileHeaderRecord hdr{};
        if (!reader.read_header(&hdr)) return false;

        out_cfg.seed             = hdr.seed;
        out_cfg.max_ticks        = hdr.max_ticks;
        out_cfg.publish_interval_ticks = static_cast<int>(hdr.publish_interval);
        // Ticker is stored as a fixed null-terminated char array.
        out_cfg.ticker = reinterpret_cast<const char*>(hdr.ticker);
        return true;
    } catch (...) {
        return false;
    }
}

bool Replay::logs_identical(const std::string& path_a, const std::string& path_b) {
    FILE* fa = std::fopen(path_a.c_str(), "rb");
    FILE* fb = std::fopen(path_b.c_str(), "rb");
    if (!fa || !fb) {
        if (fa) std::fclose(fa);
        if (fb) std::fclose(fb);
        return false;
    }

    static constexpr size_t kBuf = 65536;
    unsigned char buf_a[kBuf], buf_b[kBuf];
    bool identical = true;

    while (true) {
        size_t ra = std::fread(buf_a, 1, kBuf, fa);
        size_t rb = std::fread(buf_b, 1, kBuf, fb);
        if (ra != rb || std::memcmp(buf_a, buf_b, ra) != 0) {
            identical = false;
            break;
        }
        if (ra == 0) break; // EOF on both
    }

    std::fclose(fa);
    std::fclose(fb);
    return identical;
}

Replay::LogStats Replay::count_records(const std::string& path) {
    LogStats stats{};
    try {
        BinaryLogReader reader(path);
        if (!reader.read_header()) return stats;
        while (auto rec = reader.next()) {
            ++stats.total_records;
            std::visit([&](const auto& r) {
                using T = std::decay_t<decltype(r)>;
                if      constexpr (std::is_same_v<T, TradeRecord>)          ++stats.trades;
                else if constexpr (std::is_same_v<T, MarketSnapshotRecord>) ++stats.snapshots;
                else if constexpr (std::is_same_v<T, NewsEventRecord>)      ++stats.news_events;
                else if constexpr (std::is_same_v<T, RegimeChangeRecord>)   ++stats.regime_changes;
            }, *rec);
        }
    } catch (...) {}
    return stats;
}
