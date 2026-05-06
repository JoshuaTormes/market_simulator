#pragma once
#include "core/Types.h"
#include <string>
#include <optional>
#include <limits>

enum class OrderType {
    Limit,
    Market,
    IOC,        // Immediate-or-Cancel: fill what you can, cancel rest
    FOK,        // Fill-or-Kill: fill 100% or reject entirely
    PostOnly,   // Reject if would cross (maker-only)
    Cancel,
    Modify
};

enum class STPMode {
    CancelOldest,  // cancel the resting (maker) order
    CancelNewest,  // cancel the incoming (taker) order
    CancelBoth,
    Reject         // reject the incoming order
};

struct Order {
    OrderId   id         = 0;
    AgentId   agent_id   = 0;
    Side      side       = Side::Buy;
    OrderType type       = OrderType::Limit;
    Price     price      = 0;   // 0 = market (no price limit)
    Qty       qty        = 0;
    Tick      submit_tick= 0;
    SeqNo     seq_no     = 0;   // global monotonic — tie-breaking
    std::string ticker;
    // Optional TTL: expire after this tick (0 = no expiry, use cancel)
    Tick      ttl_expiry = 0;
};
