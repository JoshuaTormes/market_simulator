#include "PositionLedger.h"
#include <cstdlib>   // std::abs
#include <cassert>
#include <algorithm> // std::min

// ── Internal ───────────────────────────────────────────────────────────────

PositionRecord& PositionLedger::get_or_create(AgentId agent, const std::string& ticker) {
    return positions_[agent][ticker];
}

// ── Booking ───────────────────────────────────────────────────────────────

// One side of a transaction against a position record: extend, reduce or
// flip, realizing P&L on the reduced part.  Shared by apply() and seed(), so
// that an account seeded on both sides (the allocation account) nets its
// position instead of adding the two cost bases.
static Price book(PositionRecord& rec, Side side, Qty qty, Price exec_price) {
    Price realized = 0;

    if (side == Side::Buy) {
        if (rec.net_qty >= 0) {
            // Extending long (or opening from flat)
            rec.cost_basis += exec_price * qty;
            rec.net_qty    += qty;
        } else {
            // Covering short
            Qty cover = std::min(qty, -rec.net_qty);

            // Proportional basis removal for covered lots (avoids storing avg separately)
            Price basis_removed = rec.cost_basis * cover / (-rec.net_qty);
            realized = basis_removed - exec_price * cover; // short P&L: proceeds - cost-to-cover
            rec.realized_pnl += realized;
            rec.cost_basis   -= basis_removed;
            rec.net_qty      += cover;

            Qty flip = qty - cover;
            if (flip > 0) {
                // Crossed zero: now long
                rec.cost_basis = exec_price * flip;
                rec.net_qty    = flip;
            }
        }
    } else { // Side::Sell
        if (rec.net_qty <= 0) {
            // Extending short (or opening from flat)
            rec.cost_basis += exec_price * qty;
            rec.net_qty    -= qty;
        } else {
            // Reducing long
            Qty sell = std::min(qty, rec.net_qty);

            Price basis_removed = rec.cost_basis * sell / rec.net_qty;
            realized = exec_price * sell - basis_removed; // long P&L: proceeds - cost
            rec.realized_pnl += realized;
            rec.cost_basis   -= basis_removed;
            rec.net_qty      -= sell;

            Qty flip = qty - sell;
            if (flip > 0) {
                // Crossed zero: now short
                rec.cost_basis = exec_price * flip;
                rec.net_qty    = -flip;
            }
        }
    }
    return realized;
}

// ── Seed ──────────────────────────────────────────────────────────────────

void PositionLedger::seed(AgentId agent, const std::string& ticker,
                           Qty qty, Price avg_cost_per_lot) {
    if (qty == 0) return;
    auto& rec = get_or_create(agent, ticker);
    book(rec, qty > 0 ? Side::Buy : Side::Sell, std::abs(qty), avg_cost_per_lot);
}

void PositionLedger::seed_pair(AgentId agent, AgentId counterparty, const std::string& ticker,
                               Qty qty, Price avg_cost_per_lot) {
    seed(agent, ticker, qty, avg_cost_per_lot);
    seed(counterparty, ticker, -qty, avg_cost_per_lot);
}

// ── Apply trade side ──────────────────────────────────────────────────────

Price PositionLedger::apply(AgentId agent, Side side, const std::string& ticker,
                             Qty qty, Price exec_price, Price fee) {
    assert(qty > 0);

    auto& rec  = get_or_create(agent, ticker);
    rec.fees_net += fee;
    ++rec.fills;
    return book(rec, side, qty, exec_price);
}

// ── Queries ────────────────────────────────────────────────────────────────

const PositionRecord* PositionLedger::get(AgentId agent, const std::string& ticker) const {
    auto it = positions_.find(agent);
    if (it == positions_.end()) return nullptr;
    auto it2 = it->second.find(ticker);
    if (it2 == it->second.end()) return nullptr;
    return &it2->second;
}

Qty PositionLedger::net_qty(AgentId agent, const std::string& ticker) const {
    const auto* r = get(agent, ticker);
    return r ? r->net_qty : 0;
}

Price PositionLedger::avg_cost(AgentId agent, const std::string& ticker) const {
    const auto* r = get(agent, ticker);
    if (!r || r->net_qty == 0) return 0;
    return r->cost_basis / std::abs(r->net_qty);
}

Price PositionLedger::realized_pnl(AgentId agent, const std::string& ticker) const {
    const auto* r = get(agent, ticker);
    return r ? r->realized_pnl : 0;
}

Price PositionLedger::fees_net(AgentId agent, const std::string& ticker) const {
    const auto* r = get(agent, ticker);
    return r ? r->fees_net : 0;
}

Price PositionLedger::unrealized_pnl(AgentId agent, const std::string& ticker,
                                       Price mark_price) const {
    const auto* r = get(agent, ticker);
    if (!r || r->net_qty == 0) return 0;
    // Long:  (mark - avg_cost) * net_qty
    // Short: (avg_cost - mark) * (-net_qty)
    // Both expressed as: mark * net_qty - cost_basis_signed
    // cost_basis_signed = cost_basis if long, -cost_basis if short
    if (r->net_qty > 0)
        return mark_price * r->net_qty - r->cost_basis;
    else
        return r->cost_basis - mark_price * (-r->net_qty);
}

Qty PositionLedger::total_net_qty(const std::string& ticker) const {
    Qty total = 0;
    for (const auto& [agent, by_ticker] : positions_) {
        auto it = by_ticker.find(ticker);
        if (it != by_ticker.end())
            total += it->second.net_qty;
    }
    return total;
}

ZeroSumCheck PositionLedger::zero_sum(const std::string& ticker, Price mark_price) const {
    ZeroSumCheck z;
    for (const auto& [agent, by_ticker] : positions_) {
        const auto it = by_ticker.find(ticker);
        if (it == by_ticker.end()) continue;
        z.net_qty   += it->second.net_qty;
        z.gross_pnl += it->second.realized_pnl + unrealized_pnl(agent, ticker, mark_price);
        z.fees      += it->second.fees_net;
    }
    return z;
}
