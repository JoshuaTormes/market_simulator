#pragma once
// Triple-buffer for lock-free UI reads of MarketSnapshot.
// Single writer (sim thread) commits via atomic store of latest_index_.
// Single reader (UI thread) loads latest_index_ with acquire ordering.
// Per-slot mutexes prevent torn reads when writer cycles back to a slot.
#include "MarketSnapshot.h"
#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>

class SnapshotBuffer {
public:
    // Write a new snapshot (sim thread only).
    void commit(const MarketSnapshot& snap);

    // Copy the latest snapshot into `out`. Returns false if no snapshot yet.
    bool get_latest(MarketSnapshot& out) const;

    // Checksum of the latest slot: snap.tick + |snap.mid_price|.
    // Used by tests to validate read consistency.
    bool get_latest_with_checksum(MarketSnapshot& out, uint64_t& checksum_out) const;

    // True if at least one snapshot has been committed.
    bool has_snapshot() const { return latest_.load(std::memory_order_acquire) >= 0; }

    int latest_index() const { return latest_.load(std::memory_order_acquire); }

private:
    struct Slot {
        MarketSnapshot snap;
        uint64_t       checksum = 0;
    };

    std::array<Slot, 3>              slots_;
    mutable std::array<std::mutex, 3> slot_mu_;
    std::atomic<int>                 latest_{-1};
    int                              write_slot_{0};  // only touched by writer thread
};
