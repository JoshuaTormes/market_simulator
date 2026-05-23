#include "CsvExporter.h"
#include "BinaryLogReader.h"
#include <cstdio>
#include <stdexcept>
#include <string>

static FILE* open_csv(const std::string& dir, const std::string& name,
                       const char* header) {
    std::string path = dir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("CsvExporter: cannot open " + path);
    std::fputs(header, f);
    std::fputc('\n', f);
    return f;
}

CsvExporter::Stats CsvExporter::export_all(const std::string& bin_path,
                                            const std::string& out_dir) {
    BinaryLogReader reader(bin_path);
    FileHeaderRecord hdr{};
    if (!reader.read_header(&hdr))
        throw std::runtime_error("CsvExporter: invalid or incompatible log file");

    FILE* ft = open_csv(out_dir, "trades.csv",
        "tick,seq_no,price,qty,taker_side,maker_agent,taker_agent,"
        "maker_id,taker_id,fee_maker,fee_taker");
    FILE* fs = open_csv(out_dir, "snapshots.csv",
        "tick,mid_price,spread,last_trade_price,realized_vol_s,"
        "realized_vol_m,realized_vol_l,vwap_s,ofi_tick,trade_imbalance,"
        "momentum,book_imbalance_l1,regime");
    FILE* fn = open_csv(out_dir, "news.csv",
        "announce_tick,ticker,impact_log_return,duration_ticks,dispersion_sigma");
    FILE* fr = open_csv(out_dir, "regimes.csv",
        "tick,old_regime,new_regime");

    Stats stats{};

    while (auto rec = reader.next()) {
        std::visit([&](const auto& r) {
            using T = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, TradeRecord>) {
                std::fprintf(ft, "%llu,%llu,%lld,%lld,%d,%llu,%llu,%llu,%llu,%lld,%lld\n",
                    (unsigned long long)r.tick,
                    (unsigned long long)r.seq_no,
                    (long long)r.price,
                    (long long)r.qty,
                    (int)r.taker_side,
                    (unsigned long long)r.maker_agent,
                    (unsigned long long)r.taker_agent,
                    (unsigned long long)r.maker_id,
                    (unsigned long long)r.taker_id,
                    (long long)r.fee_maker,
                    (long long)r.fee_taker);
                ++stats.trades;
            }
            else if constexpr (std::is_same_v<T, MarketSnapshotRecord>) {
                std::fprintf(fs,
                    "%llu,%lld,%lld,%lld,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%d\n",
                    (unsigned long long)r.tick,
                    (long long)r.mid_price,
                    (long long)r.spread,
                    (long long)r.last_trade_price,
                    r.realized_vol_s, r.realized_vol_m, r.realized_vol_l,
                    r.vwap_s, r.ofi_tick, r.trade_imbalance,
                    r.momentum, r.book_imbalance_l1, (int)r.regime);
                ++stats.snapshots;
            }
            else if constexpr (std::is_same_v<T, NewsEventRecord>) {
                std::fprintf(fn, "%llu,%s,%.8f,%.2f,%.4f\n",
                    (unsigned long long)r.announce_tick,
                    reinterpret_cast<const char*>(r.ticker),
                    r.impact_log_return, r.duration_ticks, r.dispersion_sigma);
                ++stats.news_events;
            }
            else if constexpr (std::is_same_v<T, RegimeChangeRecord>) {
                std::fprintf(fr, "%llu,%d,%d\n",
                    (unsigned long long)r.tick, (int)r.old_regime, (int)r.new_regime);
                ++stats.regime_changes;
            }
            // FileHeader and LedgerEvent are silently skipped in CSV export.
        }, *rec);
    }

    std::fclose(ft); std::fclose(fs); std::fclose(fn); std::fclose(fr);

    if (FILE* sz_fp = std::fopen(bin_path.c_str(), "rb")) {
        std::fseek(sz_fp, 0, SEEK_END);
        stats.total_bytes_in = static_cast<uint64_t>(std::ftell(sz_fp));
        std::fclose(sz_fp);
    }

    return stats;
}
