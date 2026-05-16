#pragma once
// Buffered binary log writer.  Thread-safe: multiple threads may call write_*
// concurrently; an internal mutex serialises access to the file buffer.
// Typed write_* methods build the packed record and flush to the write buffer
// in one locked region.  Call flush() (and optionally fsync) when done.
#include "EventSchema.h"
#include "orderbook/Trade.h"
#include "marketdata/MarketSnapshot.h"
#include "economics/NewsEvent.h"
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

class BinaryLogWriter {
public:
    // Opens path for writing.  Writes the file header immediately.
    BinaryLogWriter(const std::string& path,
                    uint64_t seed,
                    uint64_t max_ticks,
                    uint64_t publish_interval,
                    const std::string& ticker);

    ~BinaryLogWriter();

    // Typed writers — each acquires the lock once.
    void write_trade   (const Trade&          t);
    void write_snapshot(const MarketSnapshot& s);
    void write_news    (const NewsEvent&       n);
    void write_regime_change(Tick tick, int old_regime, int new_regime);
    void write_ledger  (Tick tick, AgentId agent, Side side,
                        Price exec_price, Qty qty, Price fee);

    // Flush OS buffer; optional fsync for durability.
    void flush(bool do_fsync = false);

    bool is_open() const { return fp_ != nullptr; }
    uint64_t bytes_written() const { return bytes_written_; }

private:
    FILE*               fp_{nullptr};
    std::mutex          mu_;
    uint64_t            bytes_written_{0};

    // Internal: write tag + payload under the lock (caller must hold mu_).
    void write_record(EventTag tag, const void* payload, size_t sz);
};
