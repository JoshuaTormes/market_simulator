#include "BinaryLogWriter.h"
#include "RecordBuild.h"
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
    const auto r = to_record(t);
    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::Trade, &r, sizeof(r));
}

void BinaryLogWriter::write_snapshot(const MarketSnapshot& s) {
    const auto r = to_record(s);
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

void BinaryLogWriter::write_metaorder(const MetaorderEvent& m) {
    const auto r = to_record(m);
    std::lock_guard<std::mutex> lk(mu_);
    write_record(EventTag::Metaorder, &r, sizeof(r));
}

void BinaryLogWriter::flush(bool do_fsync) {
    std::lock_guard<std::mutex> lk(mu_);
    std::fflush(fp_);
    if (do_fsync) ::fsync(::fileno(fp_));
}
