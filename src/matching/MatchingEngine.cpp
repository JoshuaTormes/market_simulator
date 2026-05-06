#include "MatchingEngine.h"
#include <cmath>
#include <cassert>

MatchingEngine::MatchingEngine(
    const std::string& ticker,
    FeeModel fee_model,
    STPMode  stp,
    RngService* rng
)
    : ticker_(ticker),
      book_(fee_model),
      stp_(stp),
      rng_(rng)
{}

// ── Latency computation ────────────────────────────────────────────────────

Tick MatchingEngine::compute_arrival(Tick now, const LatencyProfile& lp) {
    Tick delay = lp.base_latency_ticks;

    if (lp.jitter_lognormal_sigma > 0.0 && rng_) {
        auto rng_eng = rng_->for_consumer("matching_jitter");
        std::lognormal_distribution<double> jitter_dist(0.0, lp.jitter_lognormal_sigma);
        delay += static_cast<Tick>(std::round(jitter_dist(rng_eng)));
    }

    return now + delay;
}

// ── Submission ────────────────────────────────────────────────────────────

void MatchingEngine::submit(Order order, Tick now, const LatencyProfile& lp) {
    order.seq_no = next_seq_++;
    Tick arrival = compute_arrival(now, lp);
    queue_.push(SubmitOrderEvent{order, arrival, order.seq_no});
}

void MatchingEngine::cancel(OrderId id, AgentId agent, Tick now, const LatencyProfile& lp) {
    SeqNo seq   = next_seq_++;
    Tick arrival = compute_arrival(now, lp);
    queue_.push(CancelOrderEvent{id, agent, arrival, seq});
}

void MatchingEngine::modify(OrderId id, AgentId agent, Price new_price, Qty new_qty,
                             Tick now, const LatencyProfile& lp) {
    SeqNo seq    = next_seq_++;
    Tick arrival = compute_arrival(now, lp);
    queue_.push(ModifyOrderEvent{id, agent, new_price, new_qty, arrival, seq});
}

// ── Process tick ──────────────────────────────────────────────────────────

void MatchingEngine::process_until(Tick now, const TradeCallback& trade_cb) {
    // Drain events with arrival_tick <= now.
    while (!queue_.empty()) {
        const MatchingEvent& top = queue_.top();
        Tick arrival = std::visit([](auto& e){ return e.arrival_tick; }, top);
        if (arrival > now) break;

        // Pop before applying (applying may push new events in future phases).
        MatchingEvent ev = queue_.top();
        queue_.pop();

        std::visit([&](auto& e){ apply_event(e, trade_cb); }, ev);
    }

    // Match any newly crossing resting orders.
    auto trades = book_.match(now);
    for (auto& tr : trades)
        trade_cb(tr);
}

// ── Event handlers ────────────────────────────────────────────────────────

void MatchingEngine::apply_event(const SubmitOrderEvent& e, const TradeCallback& trade_cb) {
    const Order& order = e.order;

    if (order.type == OrderType::Limit || order.type == OrderType::PostOnly) {
        book_.add_limit(order); // PostOnly may return nullopt (rejected) — that's fine.
    } else if (order.type == OrderType::Market
            || order.type == OrderType::IOC
            || order.type == OrderType::FOK) {
        auto trades = book_.execute_aggressive(order, e.arrival_tick);
        for (auto& tr : trades)
            trade_cb(tr);
    }
}

void MatchingEngine::apply_event(const CancelOrderEvent& e, const TradeCallback&) {
    book_.cancel(e.order_id);
}

void MatchingEngine::apply_event(const ModifyOrderEvent& e, const TradeCallback&) {
    book_.modify(e.order_id, e.new_price, e.new_qty);
}
