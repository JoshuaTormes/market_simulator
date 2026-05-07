#include "SnapshotBuffer.h"
#include <cstdlib>

void SnapshotBuffer::commit(const MarketSnapshot& snap) {
    // Choose a slot ≠ latest_ to avoid writing while reader may be copying it.
    int lat = latest_.load(std::memory_order_relaxed);
    int w   = write_slot_;
    if (w == lat) w = (w + 1) % 3;

    {
        std::lock_guard<std::mutex> lk(slot_mu_[w]);
        slots_[w].snap     = snap;
        slots_[w].checksum = static_cast<uint64_t>(snap.tick)
                           + static_cast<uint64_t>(snap.mid_price < 0
                               ? -snap.mid_price : snap.mid_price);
    }

    latest_.store(w, std::memory_order_release);
    write_slot_ = (w + 1) % 3;
}

bool SnapshotBuffer::get_latest(MarketSnapshot& out) const {
    uint64_t dummy;
    return get_latest_with_checksum(out, dummy);
}

bool SnapshotBuffer::get_latest_with_checksum(MarketSnapshot& out, uint64_t& checksum_out) const {
    int idx = latest_.load(std::memory_order_acquire);
    if (idx < 0) return false;
    std::lock_guard<std::mutex> lk(slot_mu_[idx]);
    out          = slots_[idx].snap;
    checksum_out = slots_[idx].checksum;
    return true;
}
