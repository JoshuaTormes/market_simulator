#pragma once
#include "Events.h"
#include "orderbook/OrderBookV2.h"
#include "core/RngService.h"
#include "core/LatencyProfile.h"
#include <queue>
#include <functional>
#include <vector>
#include <string>

// The matching engine:
// 1. Accepts agent actions and schedules them as future MatchingEvents (with latency).
// 2. Each tick: drains all events with arrival_tick <= now, applies them to the book.
// 3. Runs resting-order matching once per tick after events are processed.
// 4. Delivers trades via a callback to keep the engine decoupled from Clearing.
class MatchingEngine {
public:
    using TradeCallback = std::function<void(const Trade&)>;

    explicit MatchingEngine(
        const std::string& ticker,
        FeeModel fee_model = {},
        STPMode  stp       = STPMode::CancelBoth,
        RngService* rng    = nullptr
    );

    // Submit an order action (produces SubmitOrderEvent with arrival delay).
    void submit(Order order, Tick now, const LatencyProfile& latency);

    // Submit a cancel (produces CancelOrderEvent with arrival delay).
    void cancel(OrderId id, AgentId agent, Tick now, const LatencyProfile& latency);

    // Submit a modify.
    void modify(OrderId id, AgentId agent, Price new_price, Qty new_qty,
                Tick now, const LatencyProfile& latency);

    // Drain all events with arrival_tick <= now, then match resting orders.
    // Calls trade_cb for each trade produced.
    void process_until(Tick now, const TradeCallback& trade_cb);

    // Expire TTL orders.
    void expire(Tick now) { book_.expire(now); }

    const OrderBookV2& book() const { return book_; }

    SeqNo next_seq() { return next_seq_++; }

    void set_rng(RngService* rng) { rng_ = rng; }

private:
    std::string ticker_;
    OrderBookV2 book_;
    [[maybe_unused]] STPMode stp_;
    RngService* rng_;
    SeqNo next_seq_{1};

    using EventQueue = std::priority_queue<MatchingEvent,
                                           std::vector<MatchingEvent>,
                                           EventComparator>;
    EventQueue queue_;

    Tick compute_arrival(Tick now, const LatencyProfile& lp);

    void apply_event(const SubmitOrderEvent& e, const TradeCallback& cb);
    void apply_event(const CancelOrderEvent& e, const TradeCallback& cb);
    void apply_event(const ModifyOrderEvent& e, const TradeCallback& cb);
};
