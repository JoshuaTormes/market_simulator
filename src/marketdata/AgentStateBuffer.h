#pragma once
// Thread-safe snapshot of per-agent position state for UI display.
// Writer = sim thread (update); reader = UI thread (snapshot).
#include "core/Types.h"
#include <mutex>
#include <vector>

struct AgentState {
    AgentId id;
    Qty     net_qty;
    Price   realized_pnl;
    Price   unrealized_pnl;
};

class AgentStateBuffer {
public:
    void update(std::vector<AgentState> states) {
        std::lock_guard<std::mutex> lk(mu_);
        states_ = std::move(states);
    }

    std::vector<AgentState> snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return states_;
    }

private:
    mutable std::mutex mu_;
    std::vector<AgentState> states_;
};
