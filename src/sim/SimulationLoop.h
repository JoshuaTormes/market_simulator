#pragma once
// Orchestrates one full simulation tick in deterministic order.
// All components run on the sim thread; UI/logging in separate threads.
#include "core/Types.h"
#include "core/Logger.h"
#include "economics/IFundamentalValueProcess.h"
#include "economics/INewsEventProcess.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "matching/MatchingEngine.h"
#include "clearing/Clearing.h"
#include "ledger/PositionLedger.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include <atomic>
#include <string>

class SimulationLoop {
public:
    struct Config {
        Tick   max_ticks         = 100'000;
        double dt                = 1.0;   // time units per tick
        int    publish_interval  = 1;     // snapshot every N ticks
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

    Tick current_tick() const { return current_tick_; }

private:
    Config                     cfg_;
    IFundamentalValueProcess&  fundamental_;
    INewsEventProcess&         news_;
    AgentRunner&               runner_;
    RiskGate&                  risk_gate_;
    MatchingEngine&            engine_;
    Clearing&                  clearing_;
    [[maybe_unused]] PositionLedger& ledger_;  // reserved for borrow fees (Phase 7+)
    MarketDataPublisher&       publisher_;
    Logger&                    logger_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    Tick              current_tick_{0};
    OrderId           next_order_id_{1};

    MarketSnapshot    prev_snap_{};

    void tick_once(Tick now, SnapshotBuffer& snap_buf);
    void submit_filtered(const std::vector<FilteredAction>& fas, Tick now);
};
