// Full DI assembly: wires all Phase 0-8 components and runs sim + UI concurrently.
#include "core/Config.h"
#include "core/RngService.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "matching/MatchingEngine.h"
#include "ledger/PositionLedger.h"
#include "clearing/Clearing.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "marketdata/TradeTapeBuffer.h"
#include "marketdata/AgentStateBuffer.h"
#include "economics/FundamentalProcessFactory.h"
#include "economics/PoissonNewsProcess.h"
#include "agents/AgentFactory.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "sim/SimulationLoop.h"
#include "persistence/BinaryLogWriter.h"
#include "visual/VisualApp.h"
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    SimulationConfig cfg;

    for (int i = 1; i < argc - 1; ++i) {
        std::string key = argv[i];
        if (key == "--seed")     cfg.seed             = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--duration") cfg.max_ticks        = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--ticker")   cfg.ticker           = argv[i+1];
        if (key == "--output")   cfg.log_output_path  = argv[i+1];
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
    MatchingEngine    matching(cfg.ticker, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger    ledger;
    Clearing          clearing(ledger, cfg.ticker);
    // Publisher reads from the matching engine's internal book (the canonical book state).
    MarketDataPublisher publisher(matching.book());
    // Bootstrap carry-forward: the makers no longer see the fundamental, so the
    // first mid has to come from config.  Without this the maker reads mid = 0,
    // quotes nothing, the book never opens and every panel stays empty.
    publisher.set_initial_mid(static_cast<Price>(cfg.initial_price_ticks));

    // ── Fundamental value + news ─────────────────────────────────────────────
    auto fundamental_ptr = make_fundamental_process(cfg.fundamental, cfg.time,
                                                    rng.for_consumer("fundamental"));
    IFundamentalValueProcess& fundamental = *fundamental_ptr;

    PoissonNewsProcess::Config news_cfg;
    news_cfg.lambda           = cfg.news.lambda_per_tick(cfg.time);
    news_cfg.magnitude_scale  = cfg.news.impact_scale;
    news_cfg.student_t_df     = static_cast<int>(cfg.news.impact_df);
    news_cfg.duration_ticks   = static_cast<double>(cfg.news.duration_ticks);
    news_cfg.dispersion_sigma = cfg.news.dispersion;
    news_cfg.ticker           = cfg.ticker;
    PoissonNewsProcess news(news_cfg, rng.for_consumer("news"), &bus);

    // ── Agents ───────────────────────────────────────────────────────────────
    AgentFactory factory(cfg.population, rng, cfg.ticker, &bus, &ledger);
    auto owned_agents = factory.create_all();

    std::vector<IAgent*> agent_ptrs;
    agent_ptrs.reserve(owned_agents.size());
    for (auto& a : owned_agents) agent_ptrs.push_back(a.get());

    AgentRunner runner(agent_ptrs, rng.global_seed(), &ledger, cfg.ticker);
    RiskGate    risk_gate(ledger, cfg.ticker);

    // ── SimulationLoop ───────────────────────────────────────────────────────
    SimulationLoop::Config sim_cfg;
    sim_cfg.max_ticks        = cfg.max_ticks;
    sim_cfg.publish_interval = cfg.publish_interval_ticks;
    sim_cfg.ticker           = cfg.ticker;
    sim_cfg.tick_size        = cfg.tick_size;

    SnapshotBuffer   snap_buf;
    TradeTapeBuffer  tape_buf;
    AgentStateBuffer agent_buf;

    // Binary log writer (optional — only created if --output is set).
    std::unique_ptr<BinaryLogWriter> log_writer;
    if (!cfg.log_output_path.empty() && cfg.log_output_path != "sim.bin.log") {
        log_writer = std::make_unique<BinaryLogWriter>(
            cfg.log_output_path, cfg.seed, cfg.max_ticks,
            static_cast<uint64_t>(cfg.publish_interval_ticks), cfg.ticker);
    }

    SimulationLoop sim_loop(sim_cfg, fundamental, news, runner, risk_gate,
                            matching, clearing, ledger, publisher, logger);

    // Wire optional buffers.
    sim_loop.set_event_bus(&bus);
    sim_loop.set_trade_tape(&tape_buf);
    if (log_writer) sim_loop.set_log_writer(log_writer.get());
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

    if (log_writer) {
        log_writer->flush(true); // fsync on clean exit
        std::fprintf(stderr, "[INFO] Binary log: %s  (%.1f KB)\n",
                     cfg.log_output_path.c_str(),
                     log_writer->bytes_written() / 1024.0);
    }

    logger.flush();
    return 0;
}
