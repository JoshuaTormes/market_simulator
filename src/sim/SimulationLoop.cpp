// Tick order (plan §Phase6):
//  1. fundamental_process.step(dt)
//  2. news_process.step(now) → EventBus
//  3. agent_runner.run(prev_snap) → actions
//  4. risk_gate.filter(actions) → filtered
//  5. matching_engine.submit(filtered)
//  6. matching_engine.process_until(now) → trades
//  7. clearing.apply(trades)
//  8. matching_engine.expire(now)
//  9. market_data_publisher.publish(now) → new snapshot
// 10. snapshot_buffer.commit(new snapshot)
// 11. logger entry
#include "SimulationLoop.h"
#include <thread>
#include <chrono>
#include <variant>
#include <cstdio>

SimulationLoop::SimulationLoop(
    Config cfg,
    IFundamentalValueProcess& fundamental,
    INewsEventProcess&        news,
    AgentRunner&              runner,
    RiskGate&                 risk_gate,
    MatchingEngine&           engine,
    Clearing&                 clearing,
    PositionLedger&           ledger,
    MarketDataPublisher&      publisher,
    Logger&                   logger)
    : cfg_(std::move(cfg))
    , fundamental_(fundamental)
    , news_(news)
    , runner_(runner)
    , risk_gate_(risk_gate)
    , engine_(engine)
    , clearing_(clearing)
    , ledger_(ledger)
    , publisher_(publisher)
    , logger_(logger)
{}

void SimulationLoop::run(SnapshotBuffer& snap_buf) {
    for (Tick t = 0; t < cfg_.max_ticks; ++t) {
        if (stop_.load(std::memory_order_acquire)) break;

        // Pause loop: spin-wait, but honour step_count_ for single-step mode.
        while (paused_.load(std::memory_order_acquire)) {
            if (step_count_.load(std::memory_order_relaxed) > 0) {
                step_count_.fetch_sub(1, std::memory_order_relaxed);
                break; // execute one tick then re-enter the pause loop
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (stop_.load(std::memory_order_acquire)) return;
        }

        current_tick_ = t;
        tick_once(t, snap_buf);

        // Optional speed limiter.
        uint64_t delay = tick_delay_us_.load(std::memory_order_relaxed);
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
    logger_.log_info(current_tick_, "SimulationLoop finished");
}

void SimulationLoop::flush_pending_news() {
    if (!event_bus_) return;
    std::lock_guard<std::mutex> lk(pending_news_mu_);
    for (const auto& ev : pending_news_)
        event_bus_->publish(ev);
    pending_news_.clear();
}

void SimulationLoop::update_agent_state_buf(const MarketSnapshot& snap) {
    if (!agent_state_buf_ || agent_ids_.empty()) return;
    std::vector<AgentState> states;
    states.reserve(agent_ids_.size());
    for (AgentId id : agent_ids_) {
        AgentState s;
        s.id             = id;
        s.net_qty        = ledger_.net_qty(id, ticker_);
        s.realized_pnl   = ledger_.realized_pnl(id, ticker_);
        s.unrealized_pnl = ledger_.unrealized_pnl(id, ticker_, snap.mid_price);
        states.push_back(s);
    }
    agent_state_buf_->update(std::move(states));
}

void SimulationLoop::tick_once(Tick now, SnapshotBuffer& snap_buf) {
    // 1. Advance fundamental value process.
    fundamental_.step(now, cfg_.dt);

    // 2. News events (published to EventBus inside the process).
    auto news_events = news_.step(now);

    // 2b. Publish any UI-injected news (thread-safe queue).
    flush_pending_news();

    // Log news events.
    if (log_writer_) {
        for (const auto& nev : news_events)
            log_writer_->write_news(nev);
    }

    // 3. Agents observe previous snapshot and produce actions.
    // Convert fundamental value from dollar units to tick units (÷ tick_size).
    const double fundamental_ticks = fundamental_.current_value() / cfg_.tick_size;
    // Keep publisher mid-price anchored to fundamental when book is empty (no bid-ask bounce).
    publisher_.set_fundamental_price(static_cast<Price>(fundamental_ticks));
    auto agent_actions = runner_.run(prev_snap_, fundamental_ticks);

    // 4. Risk gate filters actions.
    auto filtered = risk_gate_.filter(agent_actions, prev_snap_.mid_price);

    // 5. Submit approved actions to matching engine.
    submit_filtered(filtered, now);

    // 6. Drain event queue and match orders.
    engine_.process_until(now, [this](const Trade& trade) {
        // 7. Apply each trade through clearing.
        clearing_.apply(trade);
        publisher_.on_trade(trade, current_tick_);
        // Feed trade tape for UI.
        if (trade_tape_) {
            TapeEntry e{trade.tick, trade.price, trade.qty,
                        trade.taker_side, trade.taker_agent, trade.maker_agent};
            trade_tape_->push(e);
        }
        // Persist to binary log.
        if (log_writer_) log_writer_->write_trade(trade);
    });

    // 8. Expire TTL orders.
    engine_.expire(now);

    // 9. Publish market data snapshot.
    MarketSnapshot snap = publisher_.publish(now);
    snap.regime = fundamental_.current_regime_hint();

    // Detect and log regime changes before updating prev_snap_.
    if (log_writer_ && now > 0 && snap.regime != prev_snap_.regime)
        log_writer_->write_regime_change(now, prev_snap_.regime, snap.regime);

    prev_snap_ = snap;

    // 10. Push to snapshot buffer for UI; persist to binary log.
    if (now % static_cast<Tick>(cfg_.publish_interval) == 0) {
        snap_buf.commit(snap);
        if (log_writer_) log_writer_->write_snapshot(snap);
    }

    // 10b. Update per-agent state buffer for UI.
    update_agent_state_buf(snap);

    // 11. Log per-tick summary (debug level — no-op in release).
    if (now % 1000 == 0) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "tick=%llu mid=%lld fundamental=%.2f",
            (unsigned long long)now,
            (long long)snap.mid_price,
            fundamental_.current_value());
        logger_.log_info(now, msg);
    }
}

void SimulationLoop::submit_filtered(const std::vector<FilteredAction>& fas, Tick now) {
    for (const auto& fa : fas) {
        if (!fa.approved) continue;
        IAgent*      a  = fa.agent;
        LatencyProfile lp = a->latency();

        std::visit([&](const auto& act) {
            using T = std::decay_t<decltype(act)>;
            if constexpr (std::is_same_v<T, SubmitOrder>) {
                Order order;
                order.id           = next_order_id_++;
                order.agent_id     = a->id();
                order.side         = act.side;
                order.type         = act.type;
                order.price        = act.price;
                order.qty          = act.qty;
                order.submit_tick  = now;
                order.seq_no       = engine_.next_seq();
                order.ticker       = act.ticker;
                order.ttl_expiry   = act.ttl_expiry;
                engine_.submit(order, now, lp);
            } else if constexpr (std::is_same_v<T, CancelOrder>) {
                engine_.cancel(act.order_id, a->id(), now, lp);
            } else if constexpr (std::is_same_v<T, ModifyOrder>) {
                engine_.modify(act.order_id, a->id(),
                               act.new_price, act.new_qty, now, lp);
            }
        }, fa.action);
    }
}
