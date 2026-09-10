// CLI: sim_headless [--seed N] [--duration N] [--output path] [--ticker T]
//                   [--process regime|gbm|ou|jump] [--sigma-annual X] [--news-per-day X]
//                   [--gaussian]
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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
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
        if (key == "--sigma-annual") cfg.fundamental.sigma_annual = std::stod(argv[i+1]);
        if (key == "--news-per-day")  cfg.news.events_per_day     = std::stod(argv[i+1]);
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
    auto fundamental_ptr = make_fundamental_process(cfg.fundamental, cfg.time,
                                                    rng.for_consumer("fundamental"));
    IFundamentalValueProcess& fundamental = *fundamental_ptr;

    // ── News process — fully wired from NewsConfig ────────────────────────────
    PoissonNewsProcess::Config news_cfg;
    news_cfg.lambda           = cfg.news.lambda_per_tick(cfg.time);
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

    // ── Per-agent-type summary ───────────────────────────────────────────────
    // An ecology where a whole type sits at its position limit is not trading,
    // it is a wall.  Print that before the facts so it cannot be missed.
    {
        struct TypeStats {
            int      n            = 0;
            double   abs_position = 0.0;
            double   realized_pnl = 0.0;
            uint64_t at_limit     = 0;
        };
        std::map<std::string, TypeStats> by_type;
        const auto& at_limit = runner.ticks_at_limit();

        for (IAgent* a : agent_ptrs) {
            TypeStats& ts = by_type[a->type()];
            ++ts.n;
            ts.abs_position += std::abs(static_cast<double>(ledger.net_qty(a->id(), cfg.ticker)));
            ts.realized_pnl += static_cast<double>(ledger.realized_pnl(a->id(), cfg.ticker));
            auto it = at_limit.find(a->id());
            if (it != at_limit.end()) ts.at_limit += it->second;
        }

        const double ticks = runner.ticks_run() > 0
            ? static_cast<double>(runner.ticks_run()) : 1.0;

        std::fprintf(stdout, "\nAgent-type summary\n==================\n");
        std::fprintf(stdout, "| Type                 |  n | Σ|position| | realized P&L | %% ticks at limit |\n");
        std::fprintf(stdout, "|----------------------|----|-------------|--------------|------------------|\n");
        for (const auto& [name, ts] : by_type) {
            std::fprintf(stdout, "| %-20s | %2d | %11.0f | %12.0f | %15.2f%% |\n",
                         name.c_str(), ts.n, ts.abs_position, ts.realized_pnl,
                         100.0 * static_cast<double>(ts.at_limit) / (ticks * ts.n));
        }
    }

    // ── Stylized facts report ─────────────────────────────────────────────────
    // Market-maker ids are needed so MM-vs-MM churn can be separated from real
    // trading; collect them from the live population rather than hard-coding.
    std::vector<AgentId> mm_ids;
    for (IAgent* a : agent_ptrs)
        if (std::strcmp(a->type(), "MarketMakerAS") == 0) mm_ids.push_back(a->id());

    auto report = Report::run(cfg.log_output_path, mm_ids);
    std::fprintf(stdout, "\n%s\n", report.to_text().c_str());

    return 0;
}
