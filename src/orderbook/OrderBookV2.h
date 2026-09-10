#pragma once
#include "Order.h"
#include "Trade.h"
#include "FeeModel.h"
#include "core/Types.h"
#include <map>
#include <list>
#include <set>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <string>

// A node in a price level's FIFO queue.
struct OrderNode {
    OrderId id;
    AgentId agent_id;
    Qty     qty_remaining;
    SeqNo   seq_no;
    Tick    insert_tick;
    Tick    ttl_expiry;  // 0 = no expiry
};

// Aggregated depth at one price level (for snapshots / UI).
struct DepthLevel {
    Price price;
    Qty   qty;
    int   order_count;
};

class OrderBookV2 {
public:
    explicit OrderBookV2(FeeModel fee_model = {});

    // ── Mutations ──────────────────────────────────────────────────────────
    // Add a limit order. Returns order_id on success.
    // PostOnly: returns nullopt (rejected) if it would cross.
    std::optional<OrderId> add_limit(const Order& order);

    // Match resting orders against incoming: Market / IOC / FOK.
    // Returns filled trades. For FOK, returns empty + rolls back if not 100% filled.
    std::vector<Trade> execute_aggressive(Order order, Tick now);

    // Cancel resting order by id. Returns true if found and removed.
    bool cancel(OrderId id);

    // Cancel every resting order owned by `agent`. Returns how many were removed.
    // O(k) in the agent's own order count, via the per-agent index.
    size_t cancel_all(AgentId agent);

    // Number of resting orders owned by `agent` (diagnostics and tests).
    size_t order_count(AgentId agent) const;

    // Modify price/qty of a resting order.
    // Changing price resets time priority (remove + re-insert).
    // Reducing qty only preserves priority.
    bool modify(OrderId id, Price new_price, Qty new_qty);

    // Match all crossing resting limit orders. Called once per tick.
    std::vector<Trade> match(Tick now);

    // Expire orders whose ttl_expiry <= now.
    void expire(Tick now);

    // ── Queries ────────────────────────────────────────────────────────────
    Price best_bid() const;
    Price best_ask() const;
    bool  empty_bids() const { return bids_.empty(); }
    bool  empty_asks() const { return asks_.empty(); }

    // Top-N aggregated depth levels.
    std::vector<DepthLevel> top_bids(int n) const;
    std::vector<DepthLevel> top_asks(int n) const;

    Qty total_bid_depth() const;
    Qty total_ask_depth() const;

    // Returns the 0-based position of order_id in its level's queue (for queue_position metric).
    std::optional<int> queue_position(OrderId id) const;

    // Number of active resting orders.
    size_t order_count() const { return index_.size(); }

    const FeeModel& fee_model() const { return fee_model_; }

private:
    using PriceLevel = std::list<OrderNode>;
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>; // descending
    using AskMap = std::map<Price, PriceLevel>;                       // ascending

    // Iterator bundle stored in the index for O(1) cancel.
    struct IndexEntry {
        bool is_bid;
        // We store iterators as void* to avoid the circular template issue;
        // use typed accessors below.
        BidMap::iterator bid_map_it;
        AskMap::iterator ask_map_it;
        PriceLevel::iterator level_it;
    };

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, IndexEntry> index_;
    // Owner index, kept in lockstep with index_ by index_put/index_drop.
    // std::set (not unordered) so cancel_all visits ids in a fixed order and
    // the simulation stays bit-exact across runs.
    std::unordered_map<AgentId, std::set<OrderId>> by_agent_;
    FeeModel fee_model_;

    // The only two places allowed to touch index_/by_agent_ membership.
    void index_put(const OrderNode& node, bool is_bid, Price price,
                   PriceLevel::iterator level_it);
    void index_drop(OrderId id);

    // Fill maker from its resting position (qty_remaining reduced in-place).
    // Returns the trade and cleans up the maker node if fully filled.
    Trade fill_pair(
        const Order& taker, OrderNode& maker_node,
        Price exec_price, Qty exec_qty,
        bool  maker_is_bid, Tick now, SeqNo seq
    );

    SeqNo next_trade_seq_{1};

    // Remove an empty price level from the map.
    void prune_empty_bid(BidMap::iterator it);
    void prune_empty_ask(AskMap::iterator it);
};
