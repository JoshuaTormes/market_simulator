#pragma once
#include "orderbook/Order.h"
#include "core/Types.h"
#include <variant>

// Events placed in the matching engine's priority queue.
// Ordered by (arrival_tick, seq_no) — deterministic tie-breaking.

struct SubmitOrderEvent {
    Order order;
    Tick  arrival_tick;
    SeqNo seq;
};

struct CancelOrderEvent {
    OrderId order_id;
    AgentId agent_id;
    Tick    arrival_tick;
    SeqNo   seq;
};

struct ModifyOrderEvent {
    OrderId order_id;
    AgentId agent_id;
    Price   new_price;
    Qty     new_qty;
    Tick    arrival_tick;
    SeqNo   seq;
};

// Mass cancel: drops every resting order owned by agent_id.  Queued like any
// other event, so it obeys the agent's latency and its seq places it before
// the SubmitOrders the same agent emitted after it on the same tick.
struct CancelAllEvent {
    AgentId agent_id;
    Tick    arrival_tick;
    SeqNo   seq;
};

using MatchingEvent = std::variant<SubmitOrderEvent, CancelOrderEvent,
                                   ModifyOrderEvent, CancelAllEvent>;

// Comparator: process earlier ticks first; break ties by seq_no.
struct EventComparator {
    bool operator()(const MatchingEvent& a, const MatchingEvent& b) const {
        // Returns true if a has lower priority (comes later).
        auto tick_a = std::visit([](auto& e){ return e.arrival_tick; }, a);
        auto tick_b = std::visit([](auto& e){ return e.arrival_tick; }, b);
        auto seq_a  = std::visit([](auto& e){ return e.seq;          }, a);
        auto seq_b  = std::visit([](auto& e){ return e.seq;          }, b);
        if (tick_a != tick_b) return tick_a > tick_b;
        return seq_a > seq_b;
    }
};
