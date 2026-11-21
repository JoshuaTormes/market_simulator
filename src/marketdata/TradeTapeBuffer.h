#pragma once
// Thread-safe ring buffer of the last N executed trades for UI display.
// Writer = sim thread (push); reader = UI thread (snapshot).
#include "orderbook/Trade.h"
#include <array>
#include <mutex>
#include <vector>

struct TapeEntry {
    Tick    tick;
    Price   price;
    Qty     qty;
    Side    taker_side;
    AgentId taker_agent;
    AgentId maker_agent;
};

class TradeTapeBuffer {
public:
    static constexpr int kCapacity = 200;

    void push(const TapeEntry& e) {
        std::lock_guard<std::mutex> lk(mu_);
        ring_[head_] = e;
        head_ = (head_ + 1) % kCapacity;
        if (size_ < kCapacity) ++size_;
    }

    // Returns entries in oldest-first order.
    std::vector<TapeEntry> snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<TapeEntry> out;
        out.reserve(size_);
        int start = (size_ < kCapacity) ? 0 : head_;
        for (int i = 0; i < size_; ++i)
            out.push_back(ring_[(start + i) % kCapacity]);
        return out;
    }

private:
    mutable std::mutex mu_;
    std::array<TapeEntry, kCapacity> ring_{};
    int head_ = 0;
    int size_ = 0;
};
