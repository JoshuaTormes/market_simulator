#include "BinaryLogReader.h"
#include <stdexcept>
#include <cstring>

BinaryLogReader::BinaryLogReader(const std::string& path) {
    fp_ = std::fopen(path.c_str(), "rb");
    if (!fp_)
        throw std::runtime_error("BinaryLogReader: cannot open " + path);
}

BinaryLogReader::~BinaryLogReader() {
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

bool BinaryLogReader::read_header(FileHeaderRecord* out) {
    uint8_t tag_byte;
    if (std::fread(&tag_byte, 1, 1, fp_) != 1) return false;
    if (static_cast<EventTag>(tag_byte) != EventTag::FileHeader) return false;

    FileHeaderRecord hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, fp_) != 1) return false;
    if (hdr.magic != kFileMagic) return false;
    if (hdr.version < kMinReadableVersion || hdr.version > kSchemaVersion) return false;
    version_ = hdr.version;

    ++records_read_;
    if (out) *out = hdr;
    return true;
}

std::optional<LogRecord> BinaryLogReader::next() {
    if (at_eof_ || !fp_) return std::nullopt;

    uint8_t tag_byte;
    if (std::fread(&tag_byte, 1, 1, fp_) != 1) {
        at_eof_ = true;
        return std::nullopt;
    }

    auto tag = static_cast<EventTag>(tag_byte);
    size_t sz = payload_size(tag);
    if (sz == 0) { at_eof_ = true; return std::nullopt; }

    // Snapshots grew a field in v3; older files use the frozen v2 layout.
    if (tag == EventTag::MarketSnapshot && version_ < 3) {
        MarketSnapshotRecordV2 old{};
        if (std::fread(&old, sizeof(old), 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
        ++records_read_;
        return upgrade_v2(old);
    }

    switch (tag) {
        case EventTag::FileHeader: {
            FileHeaderRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        case EventTag::Trade: {
            TradeRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        case EventTag::MarketSnapshot: {
            MarketSnapshotRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        case EventTag::NewsEvent: {
            NewsEventRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        case EventTag::RegimeChange: {
            RegimeChangeRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        case EventTag::LedgerEvent: {
            LedgerEventRecord r{};
            if (std::fread(&r, sz, 1, fp_) != 1) { at_eof_ = true; return std::nullopt; }
            ++records_read_; return r;
        }
        default:
            at_eof_ = true; return std::nullopt;
    }
}
