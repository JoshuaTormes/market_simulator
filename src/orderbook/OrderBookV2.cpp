#include "OrderBookV2.h"
#include <algorithm>
#include <cassert>

OrderBookV2::OrderBookV2(FeeModel fee_model)
    : fee_model_(fee_model) {}

// ── Internal helpers ───────────────────────────────────────────────────────

void OrderBookV2::prune_empty_bid(BidMap::iterator it) {
    if (it->second.empty()) bids_.erase(it);
}

void OrderBookV2::prune_empty_ask(AskMap::iterator it) {
    if (it->second.empty()) asks_.erase(it);
}

// index_ and by_agent_ must never disagree: a stale owner entry would make
// cancel_all() try to remove an order that is already gone.  Every insertion
// and removal goes through these two.
void OrderBookV2::index_put(const OrderNode& node, bool is_bid, Price price,
                            PriceLevel::iterator level_it) {
    if (is_bid) index_[node.id] = IndexEntry{true, bids_.find(price), {}, level_it};
    else        index_[node.id] = IndexEntry{false, {}, asks_.find(price), level_it};
    by_agent_[node.agent_id].insert(node.id);
}

void OrderBookV2::index_drop(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    const AgentId owner = it->second.level_it->agent_id;
    index_.erase(it);
    auto ait = by_agent_.find(owner);
    if (ait != by_agent_.end()) {
        ait->second.erase(id);
        if (ait->second.empty()) by_agent_.erase(ait);
    }
}

Trade OrderBookV2::fill_pair(
    const Order& taker, OrderNode& maker,
    Price exec_price, Qty exec_qty,
    bool /*maker_is_bid*/, Tick now, SeqNo seq
) {
    Trade t;
    t.price       = exec_price;
    t.qty         = exec_qty;
    t.taker_side  = taker.side;
    t.tick        = now;
    t.seq_no      = seq;
    t.fee_taker   = fee_model_.taker_fee(exec_qty);
    t.fee_maker   = -fee_model_.maker_rebate(exec_qty); // negative = rebate credit

    if (taker.side == Side::Buy) {
        t.maker_id    = maker.id;
        t.taker_id    = taker.id;
        t.maker_agent = maker.agent_id;
        t.taker_agent = taker.agent_id;
    } else {
        t.maker_id    = maker.id;
        t.taker_id    = taker.id;
        t.maker_agent = maker.agent_id;
        t.taker_agent = taker.agent_id;
    }

    maker.qty_remaining -= exec_qty;
    return t;
}

// ── Add limit order ────────────────────────────────────────────────────────

std::optional<OrderId> OrderBookV2::add_limit(const Order& order) {
    assert(order.qty > 0);
    assert(order.type == OrderType::Limit || order.type == OrderType::PostOnly);

    // PostOnly: reject if it would immediately cross.
    if (order.type == OrderType::PostOnly) {
        if (order.side == Side::Buy && !asks_.empty() && order.price >= asks_.begin()->first)
            return std::nullopt;
        if (order.side == Side::Sell && !bids_.empty() && order.price <= bids_.begin()->first)
            return std::nullopt;
    }

    OrderNode node{
        order.id,
        order.agent_id,
        order.qty,
        order.seq_no,
        order.submit_tick,
        order.ttl_expiry
    };

    if (order.side == Side::Buy) {
        auto& level = bids_[order.price];
        auto  it    = level.insert(level.end(), node);
        index_put(node, true, order.price, it);
    } else {
        auto& level = asks_[order.price];
        auto  it    = level.insert(level.end(), node);
        index_put(node, false, order.price, it);
    }

    return order.id;
}

// ── Match resting orders ───────────────────────────────────────────────────

std::vector<Trade> OrderBookV2::match(Tick now) {
    std::vector<Trade> trades;

    while (!bids_.empty() && !asks_.empty()) {
        auto bid_lvl_it = bids_.begin();   // highest bid
        auto ask_lvl_it = asks_.begin();   // lowest ask

        if (bid_lvl_it->first < ask_lvl_it->first) break; // no cross

        PriceLevel& bid_level = bid_lvl_it->second;
        PriceLevel& ask_level = ask_lvl_it->second;

        assert(!bid_level.empty() && !ask_level.empty());

        OrderNode& bid_node = bid_level.front();
        OrderNode& ask_node = ask_level.front();

        // Self-trade prevention: CancelBoth (default for resting-vs-resting).
        if (bid_node.agent_id == ask_node.agent_id) {
            index_drop(bid_node.id);
            index_drop(ask_node.id);
            bid_level.pop_front();
            ask_level.pop_front();
            prune_empty_bid(bid_lvl_it);
            prune_empty_ask(ask_lvl_it);
            continue;
        }

        // Price-time priority: maker = whoever rested first (lower seq_no).
        // In a resting-vs-resting cross, the older order is maker;
        // execution price = the older order's price.
        bool bid_is_maker = (bid_node.seq_no < ask_node.seq_no);
        Price exec_price  = bid_is_maker ? bid_lvl_it->first : ask_lvl_it->first;

        Qty exec_qty = std::min(bid_node.qty_remaining, ask_node.qty_remaining);

        // Build trade — maker is the older order.
        Trade tr;
        tr.price       = exec_price;
        tr.qty         = exec_qty;
        tr.tick        = now;
        tr.seq_no      = next_trade_seq_++;
        tr.fee_taker   = fee_model_.taker_fee(exec_qty);
        tr.fee_maker   = -fee_model_.maker_rebate(exec_qty);
        if (bid_is_maker) {
            tr.maker_id    = bid_node.id;    tr.maker_agent = bid_node.agent_id;
            tr.taker_id    = ask_node.id;    tr.taker_agent = ask_node.agent_id;
            tr.taker_side  = Side::Sell;
        } else {
            tr.maker_id    = ask_node.id;    tr.maker_agent = ask_node.agent_id;
            tr.taker_id    = bid_node.id;    tr.taker_agent = bid_node.agent_id;
            tr.taker_side  = Side::Buy;
        }

        bid_node.qty_remaining -= exec_qty;
        ask_node.qty_remaining -= exec_qty;

        trades.push_back(tr);

        // Clean up fully filled nodes (preserve partial-fill position by NOT re-inserting).
        if (bid_node.qty_remaining == 0) {
            index_drop(bid_node.id);
            bid_level.pop_front();
            prune_empty_bid(bid_lvl_it);
        }
        if (ask_node.qty_remaining == 0) {
            index_drop(ask_node.id);
            ask_level.pop_front();
            prune_empty_ask(ask_lvl_it);
        }
    }

    return trades;
}

// ── Execute aggressive order (Market / IOC / FOK) ─────────────────────────

std::vector<Trade> OrderBookV2::execute_aggressive(Order order, Tick now) {
    std::vector<Trade> trades;

    // FOK: check fill feasibility before touching the book.
    if (order.type == OrderType::FOK) {
        Qty available = 0;
        if (order.side == Side::Buy) {
            for (auto& [p, lvl] : asks_) {
                for (auto& node : lvl) available += node.qty_remaining;
                if (available >= order.qty) break;
            }
        } else {
            for (auto& [p, lvl] : bids_) {
                for (auto& node : lvl) available += node.qty_remaining;
                if (available >= order.qty) break;
            }
        }
        if (available < order.qty) return {}; // FOK rejected — book untouched
    }

    Qty remaining = order.qty;

    auto fill_side = [&](auto& book_map) {
        while (remaining > 0 && !book_map.empty()) {
            auto lvl_it = book_map.begin();
            PriceLevel& level = lvl_it->second;

            while (remaining > 0 && !level.empty()) {
                OrderNode& maker = level.front();

                if (maker.agent_id == order.agent_id) {
                    // Self-trade: skip this maker (CancelNewest = skip incoming partial).
                    // For a market/IOC/FOK we just skip this level node.
                    index_drop(maker.id);
                    level.pop_front();
                    continue;
                }

                Qty exec_qty  = std::min(remaining, maker.qty_remaining);
                Price exec_price = lvl_it->first; // maker price

                Trade tr;
                tr.price       = exec_price;
                tr.qty         = exec_qty;
                tr.maker_id    = maker.id;
                tr.taker_id    = order.id;
                tr.taker_side  = order.side;
                tr.maker_agent = maker.agent_id;
                tr.taker_agent = order.agent_id;
                tr.tick        = now;
                tr.seq_no      = next_trade_seq_++;
                tr.fee_taker   = fee_model_.taker_fee(exec_qty);
                tr.fee_maker   = -fee_model_.maker_rebate(exec_qty);

                maker.qty_remaining -= exec_qty;
                remaining           -= exec_qty;

                trades.push_back(tr);

                if (maker.qty_remaining == 0) {
                    index_drop(maker.id);
                    level.pop_front();
                }
            }

            if (level.empty()) book_map.erase(lvl_it);
        }
    };

    if (order.side == Side::Buy)  fill_side(asks_);
    else                           fill_side(bids_);

    // IOC: any unfilled residual is silently dropped (already removed from qty).
    // Market: same.
    // FOK: already guaranteed full fill above.

    return trades;
}

// ── Cancel ────────────────────────────────────────────────────────────────

bool OrderBookV2::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;

    IndexEntry entry = it->second;
    // Drop from the indices first: index_drop reads the owner off the node,
    // which the list erase below invalidates.
    index_drop(id);

    if (entry.is_bid) {
        entry.bid_map_it->second.erase(entry.level_it);
        prune_empty_bid(entry.bid_map_it);
    } else {
        entry.ask_map_it->second.erase(entry.level_it);
        prune_empty_ask(entry.ask_map_it);
    }

    return true;
}

size_t OrderBookV2::cancel_all(AgentId agent) {
    auto it = by_agent_.find(agent);
    if (it == by_agent_.end()) return 0;

    // Copy the ids: cancel() mutates by_agent_ as it goes.
    const std::vector<OrderId> ids(it->second.begin(), it->second.end());
    size_t removed = 0;
    for (OrderId id : ids)
        if (cancel(id)) ++removed;
    return removed;
}

size_t OrderBookV2::order_count(AgentId agent) const {
    auto it = by_agent_.find(agent);
    return it == by_agent_.end() ? 0 : it->second.size();
}

// ── Modify ────────────────────────────────────────────────────────────────

bool OrderBookV2::modify(OrderId id, Price new_price, Qty new_qty) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;

    IndexEntry& entry = it->second;
    OrderNode& node = *entry.level_it;

    Price old_price = entry.is_bid ? entry.bid_map_it->first : entry.ask_map_it->first;

    if (new_price != old_price) {
        // Price change: cancel + re-insert at tail of new level (lose time priority).
        Order o;
        o.id          = id;
        o.agent_id    = node.agent_id;
        o.side        = entry.is_bid ? Side::Buy : Side::Sell;
        o.type        = OrderType::Limit;
        o.price       = new_price;
        o.qty         = new_qty;
        o.seq_no      = node.seq_no; // keep original seq for tracking; priority resets positionally
        o.submit_tick = node.insert_tick;
        o.ttl_expiry  = node.ttl_expiry;

        cancel(id);
        add_limit(o);
    } else if (new_qty < node.qty_remaining) {
        // Reduce-only: update in-place, preserve position.
        node.qty_remaining = new_qty;
    } else if (new_qty > node.qty_remaining) {
        // Increase: must cancel + re-insert (lose priority per exchange convention).
        Order o;
        o.id          = id;
        o.agent_id    = node.agent_id;
        o.side        = entry.is_bid ? Side::Buy : Side::Sell;
        o.type        = OrderType::Limit;
        o.price       = new_price;
        o.qty         = new_qty;
        o.seq_no      = node.seq_no;
        o.submit_tick = node.insert_tick;
        o.ttl_expiry  = node.ttl_expiry;

        cancel(id);
        add_limit(o);
    }

    return true;
}

// ── Expire ────────────────────────────────────────────────────────────────

void OrderBookV2::expire(Tick now) {
    std::vector<OrderId> to_cancel;

    for (auto& [id, entry] : index_) {
        Tick ttl = entry.level_it->ttl_expiry;
        if (ttl != 0 && ttl <= now)
            to_cancel.push_back(id);
    }

    for (OrderId id : to_cancel)
        cancel(id);
}

// ── Queries ────────────────────────────────────────────────────────────────

Price OrderBookV2::best_bid() const {
    return bids_.empty() ? 0 : bids_.begin()->first;
}

Price OrderBookV2::best_ask() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

std::vector<DepthLevel> OrderBookV2::top_bids(int n) const {
    std::vector<DepthLevel> out;
    out.reserve(n);
    for (auto& [price, level] : bids_) {
        if (static_cast<int>(out.size()) >= n) break;
        Qty total = 0;
        for (auto& node : level) total += node.qty_remaining;
        out.push_back({price, total, static_cast<int>(level.size())});
    }
    return out;
}

std::vector<DepthLevel> OrderBookV2::top_asks(int n) const {
    std::vector<DepthLevel> out;
    out.reserve(n);
    for (auto& [price, level] : asks_) {
        if (static_cast<int>(out.size()) >= n) break;
        Qty total = 0;
        for (auto& node : level) total += node.qty_remaining;
        out.push_back({price, total, static_cast<int>(level.size())});
    }
    return out;
}

Qty OrderBookV2::total_bid_depth() const {
    Qty total = 0;
    for (auto& [p, lvl] : bids_)
        for (auto& n : lvl) total += n.qty_remaining;
    return total;
}

Qty OrderBookV2::total_ask_depth() const {
    Qty total = 0;
    for (auto& [p, lvl] : asks_)
        for (auto& n : lvl) total += n.qty_remaining;
    return total;
}

std::optional<int> OrderBookV2::queue_position(OrderId id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;

    const IndexEntry& entry = it->second;
    const PriceLevel& level = entry.is_bid
        ? entry.bid_map_it->second
        : entry.ask_map_it->second;

    int pos = 0;
    for (auto lit = level.begin(); lit != level.end(); ++lit, ++pos)
        if (lit->id == id) return pos;

    return std::nullopt;
}
