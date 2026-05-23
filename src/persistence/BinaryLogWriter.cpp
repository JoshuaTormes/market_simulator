#include "BinaryLogWriter.h"
#include <cstring>
#include <stdexcept>
#include <unistd.h>  // fsync, fileno

BinaryLogWriter::BinaryLogWriter(const std::string& path,
                                 uint64_t seed,
                                 uint64_t max_ticks,
                                 uint64_t publish_interval,
                                 const std::string& ticker)
{
    fp_ = std::fopen(path.c_str(), "wb");
    if (!fp_)
        throw std::runtime_error("BinaryLogWriter: cannot open " + path);

    // Write file header record immediately.
    FileHeaderRecord hdr{};
    hdr.magic            = kFileMagic;
    hdr.version          = kSchemaVersion;
    hdr.seed             = seed;
    hdr.max_ticks        = max_ticks;
    hdr.publish_interval = publish_interval;
    std::strncpy(reinterpret_cast<char*>(hdr.ticker),
                 ticker.c_str(), sizeof(hdr.ticker) - 1);
    write_record(EventTag::FileHeader, &hdr, sizeof(hdr));
}

BinaryLogWriter::~BinaryLogWriter() {
    if (fp_) {
        std::fflush(fp_);
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

void BinaryLogWriter::write_record(EventTag tag, const void* payload, size_t sz) {
    uint8_t t = static_cast<uint8_t>(tag);
    std::fwrite(&t, 1, 1, fp_);
    std::fwrite(payload, 1, sz, fp_);
    bytes_written_ += 1 + sz;
}

void BinaryLogWriter::write_trade(const Trade& t) {
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

    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::Trade, &r, sizeof(r));
}

void BinaryLogWriter::write_snapshot(const MarketSnapshot& s) {
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
    r.regime                = static_cast<int8_t>(s.regime);

    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::MarketSnapshot, &r, sizeof(r));
}

void BinaryLogWriter::write_news(const NewsEvent& n) {
    NewsEventRecord r{};
    r.announce_tick      = n.announce_tick;
    r.impact_log_return  = n.impact_log_return;
    r.duration_ticks     = n.duration_ticks;
    r.dispersion_sigma   = n.dispersion_sigma;
    std::strncpy(reinterpret_cast<char*>(r.ticker),
                 n.ticker.c_str(), sizeof(r.ticker) - 1);

    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::NewsEvent, &r, sizeof(r));
}

void BinaryLogWriter::write_regime_change(Tick tick, int old_r, int new_r) {
    RegimeChangeRecord r{};
    r.tick       = tick;
    r.old_regime = static_cast<int8_t>(old_r);
    r.new_regime = static_cast<int8_t>(new_r);

    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::RegimeChange, &r, sizeof(r));
}

void BinaryLogWriter::write_ledger(Tick tick, AgentId agent, Side side,
                                    Price exec_price, Qty qty, Price fee) {
    LedgerEventRecord r{};
    r.tick       = tick;
    r.agent_id   = agent;
    r.side       = (side == Side::Buy) ? 0 : 1;
    r.exec_price = exec_price;
    r.qty        = qty;
    r.fee        = fee;

    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::LedgerEvent, &r, sizeof(r));
}

void BinaryLogWriter::flush(bool do_fsync) {
    std::lock_guard<std::mutex> lk(mu_);
    std::fflush(fp_);
    if (do_fsync) ::fsync(::fileno(fp_));
}
