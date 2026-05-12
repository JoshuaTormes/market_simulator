// Full DI assembly: wires all Phase 0-7 components and runs sim + UI concurrently.
#include "core/Config.h"
#include "core/RngService.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "orderbook/OrderBookV2.h"
#include "matching/MatchingEngine.h"
#include "ledger/PositionLedger.h"
#include "clearing/Clearing.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "marketdata/TradeTapeBuffer.h"
#include "marketdata/AgentStateBuffer.h"
#include "economics/RegimeSwitchingProcess.h"
#include "economics/PoissonNewsProcess.h"
#include "agents/AgentFactory.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "sim/SimulationLoop.h"
#include "visual/VisualApp.h"
#include <cstdio>
#include <thread>
#include <string>
#include <memory>
#include <vector>

int main(int argc, char** argv) {
    SimulationConfig cfg;

    for (int i = 1; i < argc - 1; ++i) {
        std::string key = argv[i];
        if (key == "--seed")     cfg.seed      = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--duration") cfg.max_ticks = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--ticker")   cfg.ticker    = argv[i+1];
    }

    // ── Infra ────────────────────────────────────────────────────────────────
    RngService rng(cfg.seed);

    Logger logger([](const LogEntry& e) {
        const char* lvl = e.level == LogLevel::ERROR ? "ERROR"
                        : e.level == LogLevel::WARN  ? "WARN"
                        : e.level == LogLevel::INFO  ? "INFO" : "DEBUG";
        std::fprintf(stderr, "[%s t=%llu] %s\n", lvl,
                     (unsigned long long)e.tick, e.msg);
    });

    EventBus bus;

    // ── Market mechanics ─────────────────────────────────────────────────────
    OrderBookV2       book;
    MatchingEngine    matching(cfg.ticker, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger    ledger;
    Clearing          clearing(ledger, cfg.ticker);
    MarketDataPublisher publisher(book);

    // ── Fundamental value + news ─────────────────────────────────────────────
    RegimeSwitchingProcess::Config rs_cfg;
    rs_cfg.s0 = cfg.fundamental.initial_value;
    RegimeSwitchingProcess fundamental(rs_cfg, rng.for_consumer("fundamental"));

    PoissonNewsProcess::Config news_cfg;
    news_cfg.lambda          = cfg.news.lambda;
    news_cfg.magnitude_scale = cfg.news.impact_scale;
    news_cfg.ticker          = cfg.ticker;
    PoissonNewsProcess news(news_cfg, rng.for_consumer("news"), &bus);

    // ── Agents ───────────────────────────────────────────────────────────────
    AgentFactory factory(cfg.population, rng, cfg.ticker, &bus);
    auto owned_agents = factory.create_all();

    std::vector<IAgent*> agent_ptrs;
    agent_ptrs.reserve(owned_agents.size());
    for (auto& a : owned_agents) agent_ptrs.push_back(a.get());

    AgentRunner runner(agent_ptrs);
    RiskGate    risk_gate(ledger, cfg.ticker);

    // ── SimulationLoop ───────────────────────────────────────────────────────
    SimulationLoop::Config sim_cfg;
    sim_cfg.max_ticks        = cfg.max_ticks;
    sim_cfg.publish_interval = cfg.publish_interval_ticks;
    sim_cfg.ticker           = cfg.ticker;

    SnapshotBuffer   snap_buf;
    TradeTapeBuffer  tape_buf;
    AgentStateBuffer agent_buf;

    SimulationLoop sim_loop(sim_cfg, fundamental, news, runner, risk_gate,
                            matching, clearing, ledger, publisher, logger);

    // Wire optional UI buffers.
    sim_loop.set_event_bus(&bus);
    sim_loop.set_trade_tape(&tape_buf);
    {
        std::vector<AgentId> ids;
        ids.reserve(owned_agents.size());
        for (auto& a : owned_agents) ids.push_back(a->id());
        sim_loop.set_agent_state_buf(&agent_buf, std::move(ids), cfg.ticker);
    }

    // ── Run sim thread + UI on main thread ───────────────────────────────────
    std::thread sim_thread([&] { sim_loop.run(snap_buf); });

    VisualApp app(snap_buf, tape_buf, agent_buf, sim_loop, bus, fundamental);
    app.run();

    sim_loop.request_stop();
    sim_thread.join();

    logger.flush();
    return 0;
}
