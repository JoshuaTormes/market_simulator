#pragma once
// Orchestrates one full simulation tick in deterministic order.
// All components run on the sim thread; UI/logging in separate threads.
#include "core/Types.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "economics/IFundamentalValueProcess.h"
#include "economics/INewsEventProcess.h"
#include "economics/NewsEvent.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "matching/MatchingEngine.h"
#include "clearing/Clearing.h"
#include "ledger/PositionLedger.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "marketdata/TradeTapeBuffer.h"
#include "marketdata/AgentStateBuffer.h"
#include "persistence/BinaryLogWriter.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class SimulationLoop {
public:
    struct Config {
        Tick        max_ticks        = 100'000;
        double      dt               = 1.0;   // time units per tick
        int         publish_interval = 1;     // snapshot every N ticks
        std::string ticker           = "MAIN";
        double      tick_size        = 0.01;  // dollars per tick — used to convert fundamental→ticks
    };

    SimulationLoop(
        Config                     cfg,
        IFundamentalValueProcess&  fundamental,
        INewsEventProcess&         news,
        AgentRunner&               runner,
        RiskGate&                  risk_gate,
        MatchingEngine&            engine,
        Clearing&                  clearing,
        PositionLedger&            ledger,
        MarketDataPublisher&       publisher,
        Logger&                    logger
    );

    // Run until max_ticks or stop() is called. Publishes to snap_buf.
    void run(SnapshotBuffer& snap_buf);

    void request_stop() { stop_.store(true,   std::memory_order_release); }
    void pause()        { paused_.store(true,  std::memory_order_release); }
    void resume()       { paused_.store(false, std::memory_order_release); }
    bool is_paused()    const { return paused_.load(std::memory_order_acquire); }

    // Execute N more ticks while paused, then re-pause.
    void step(Tick n = 1) { step_count_.fetch_add(n, std::memory_order_relaxed); }

    // Slow down the sim: sleep this many µs after each tick (0 = full speed).
    void set_tick_delay_us(uint64_t us) { tick_delay_us_.store(us, std::memory_order_relaxed); }

    // Wire optional UI-facing buffers (call before run()).
    void set_event_bus(EventBus* bus)                           { event_bus_ = bus; }
    void set_trade_tape(TradeTapeBuffer* buf)                   { trade_tape_ = buf; }
    void set_agent_state_buf(AgentStateBuffer* buf,
                             std::vector<AgentId> ids,
                             std::string ticker)                {
        agent_state_buf_ = buf;
        agent_ids_       = std::move(ids);
        ticker_          = std::move(ticker);
    }
    void set_log_writer(BinaryLogWriter* writer) { log_writer_ = writer; }

    // Thread-safe: queue a news event to be published on the next sim tick.
    void inject_news(const NewsEvent& ev) {
        std::lock_guard<std::mutex> lk(pending_news_mu_);
        pending_news_.push_back(ev);
    }

    Tick current_tick() const { return current_tick_; }

private:
    Config                     cfg_;
    IFundamentalValueProcess&  fundamental_;
    INewsEventProcess&         news_;
    AgentRunner&               runner_;
    RiskGate&                  risk_gate_;
    MatchingEngine&            engine_;
    Clearing&                  clearing_;
    PositionLedger&            ledger_;
    MarketDataPublisher&       publisher_;
    Logger&                    logger_;

    // Optional UI-facing buffers (nullptr = disabled)
    EventBus*         event_bus_{nullptr};
    TradeTapeBuffer*  trade_tape_{nullptr};
    AgentStateBuffer* agent_state_buf_{nullptr};
    BinaryLogWriter*  log_writer_{nullptr};
    std::vector<AgentId> agent_ids_;
    std::string      ticker_;

    // Runtime controls
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     paused_{false};
    std::atomic<Tick>     step_count_{0};
    std::atomic<uint64_t> tick_delay_us_{0};

    // Pending UI-injected news (mutex-protected, flushed each tick)
    std::mutex             pending_news_mu_;
    std::vector<NewsEvent> pending_news_;

    Tick              current_tick_{0};
    OrderId           next_order_id_{1};
    MarketSnapshot    prev_snap_{};

    void tick_once(Tick now, SnapshotBuffer& snap_buf);
    void submit_filtered(const std::vector<FilteredAction>& fas, Tick now);
    void flush_pending_news();
    void update_agent_state_buf(const MarketSnapshot& snap);
};
