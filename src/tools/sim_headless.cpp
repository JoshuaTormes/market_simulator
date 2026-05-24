// CLI: sim_headless [--seed N] [--duration N] [--output path] [--ticker T] [--process regime|gbm|ou|jump]
// Runs the full simulation headlessly (no UI) and writes a binary log.
// Uses the same component wiring as main.cpp but without SFML/ImGui.
#include "analysis/Report.h"
#include "core/Config.h"
#include "core/RngService.h"
#include "core/Logger.h"
#include "core/EventBus.h"
#include "orderbook/Order.h"
#include "matching/MatchingEngine.h"
#include "ledger/PositionLedger.h"
#include "clearing/Clearing.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "economics/FundamentalProcessFactory.h"
#include "economics/PoissonNewsProcess.h"
#include "agents/AgentFactory.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "sim/SimulationLoop.h"
#include "persistence/BinaryLogWriter.h"
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    SimulationConfig cfg;
    cfg.max_ticks = 100'000;
    cfg.log_output_path = "logs/run_seed42.bin";

    for (int i = 1; i < argc - 1; ++i) {
        std::string key = argv[i];
        if (key == "--seed")     cfg.seed            = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--duration") cfg.max_ticks       = static_cast<uint64_t>(std::stoul(argv[i+1]));
        if (key == "--output")   cfg.log_output_path = argv[i+1];
        if (key == "--ticker")   cfg.ticker          = argv[i+1];
        if (key == "--process") {
            std::string proc = argv[i+1];
            if      (proc == "gbm")    cfg.fundamental.type = FundamentalProcessType::GBM;
            else if (proc == "ou")     cfg.fundamental.type = FundamentalProcessType::OU;
            else if (proc == "jump")   cfg.fundamental.type = FundamentalProcessType::JumpDiffusion;
            else                       cfg.fundamental.type = FundamentalProcessType::RegimeSwitching;
        }
        if (key == "--gaussian") cfg.fundamental.gaussian_innovations = true;
    }

    std::filesystem::create_directories(
        std::filesystem::path(cfg.log_output_path).parent_path());

    std::fprintf(stderr, "[INFO] sim_headless: seed=%llu  ticks=%llu  output=%s\n",
                 (unsigned long long)cfg.seed,
                 (unsigned long long)cfg.max_ticks,
                 cfg.log_output_path.c_str());

    // ── Infra ────────────────────────────────────────────────────────────────
    RngService rng(cfg.seed);
    Logger     logger([](const LogEntry& e) {
        if (e.level >= LogLevel::WARN)
            std::fprintf(stderr, "[%s t=%llu] %s\n",
                         e.level == LogLevel::ERROR ? "ERROR" : "WARN",
                         (unsigned long long)e.tick, e.msg);
    });
    EventBus bus;

    // ── Market mechanics ─────────────────────────────────────────────────────
    MatchingEngine      matching(cfg.ticker, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger      ledger;
    Clearing            clearing(ledger, cfg.ticker);
    MarketDataPublisher publisher(matching.book());
    // Bootstrap carry-forward: MMs no longer see the fundamental, so we set the
    // initial mid from config. This gives the MM a non-zero price anchor on tick 0.
    publisher.set_initial_mid(static_cast<Price>(cfg.initial_price_ticks));

    // ── Fundamental value: selected via --process flag ────────────────────────
    auto fundamental_ptr = make_fundamental_process(cfg.fundamental,
                                                    rng.for_consumer("fundamental"));
    IFundamentalValueProcess& fundamental = *fundamental_ptr;

    // ── News process — fully wired from NewsConfig ────────────────────────────
    PoissonNewsProcess::Config news_cfg;
    news_cfg.lambda           = cfg.news.lambda;
    news_cfg.magnitude_scale  = cfg.news.impact_scale;
    news_cfg.student_t_df     = static_cast<int>(cfg.news.impact_df);
    news_cfg.duration_ticks   = static_cast<double>(cfg.news.duration_ticks);
    news_cfg.dispersion_sigma = cfg.news.dispersion;
    news_cfg.ticker           = cfg.ticker;
    PoissonNewsProcess news(news_cfg, rng.for_consumer("news"), &bus);

    // ── Agents: full default population ──────────────────────────────────────
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

    BinaryLogWriter log_writer(
        cfg.log_output_path, cfg.seed, cfg.max_ticks,
        static_cast<uint64_t>(cfg.publish_interval_ticks), cfg.ticker);

    SnapshotBuffer snap_buf;
    SimulationLoop sim_loop(sim_cfg, fundamental, news, runner, risk_gate,
                            matching, clearing, ledger, publisher, logger);
    sim_loop.set_event_bus(&bus);
    sim_loop.set_log_writer(&log_writer);

    sim_loop.run(snap_buf);
    log_writer.flush(true);

    std::fprintf(stderr, "[INFO] Done. Log: %s  (%.1f KB)\n",
                 cfg.log_output_path.c_str(),
                 log_writer.bytes_written() / 1024.0);

    // ── Stylized facts report ─────────────────────────────────────────────────
    auto report = Report::run(cfg.log_output_path);
    std::fprintf(stdout, "\n%s\n", report.to_text().c_str());

    return 0;
}
