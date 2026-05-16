#pragma once
// Stream-based binary log reader.  Provides an iterator over typed LogRecords.
// Usage:
//   BinaryLogReader reader("run.bin");
//   reader.read_header(); // validates magic + version
//   while (auto rec = reader.next()) {
//       std::visit(overload{...}, *rec);
//   }
#include "EventSchema.h"
#include <cstdio>
#include <optional>
#include <string>
#include <variant>

// Typed record returned by BinaryLogReader::next().
using LogRecord = std::variant<
    FileHeaderRecord,
    TradeRecord,
    MarketSnapshotRecord,
    NewsEventRecord,
    RegimeChangeRecord,
    LedgerEventRecord
>;

class BinaryLogReader {
public:
    explicit BinaryLogReader(const std::string& path);
    ~BinaryLogReader();

    // Read and validate the file header.  Must be called first.
    // Returns false if magic or version mismatch.
    bool read_header(FileHeaderRecord* out = nullptr);

    // Read the next record.  Returns std::nullopt on EOF or error.
    std::optional<LogRecord> next();

    bool is_open()  const { return fp_ != nullptr; }
    bool at_eof()   const { return at_eof_; }
    uint64_t records_read() const { return records_read_; }

private:
    FILE*    fp_{nullptr};
    bool     at_eof_{false};
    uint64_t records_read_{0};
};
