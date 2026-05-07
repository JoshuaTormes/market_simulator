#pragma once
#include "core/Types.h"
#include "orderbook/Order.h"
#include <variant>
#include <string>

// Agent action types: the agent's intent expressed per tick.
// AgentRunner converts approved Actions into MatchingEngine events.

struct SubmitOrder {
    Side        side;
    OrderType   type;
    Price       price;       // 0 = market (no price limit)
    Qty         qty;
    std::string ticker;
    Tick        ttl_expiry = 0;  // 0 = no expiry; set >0 for GTT orders
};

struct CancelOrder {
    OrderId     order_id;
    std::string ticker;
};

struct ModifyOrder {
    OrderId     order_id;
    Price       new_price;
    Qty         new_qty;
    std::string ticker;
};

using Action = std::variant<SubmitOrder, CancelOrder, ModifyOrder>;
