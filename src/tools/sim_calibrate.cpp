// sim_calibrate [--seeds N] [--duration T] [--outdir path] [--ticker T] [--process P]
// Ensemble calibration harness: runs N seeds, aggregates mean ± std for each
// stylized-fact metric and prints a summary table.
#include "analysis/Report.h"
#include "core/Config.h"
#include "core/RngService.h"
#include "core/Logger.h"
#include "core/EventBus.h"
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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

// ── Run one seed and return its report ───────────────────────────────────────

static AnalysisReport run_one(const SimulationConfig& base_cfg,
                              uint64_t seed,
                              const std::string& log_path) {
    SimulationConfig cfg = base_cfg;
    cfg.seed             = seed;
    cfg.log_output_path  = log_path;

    RngService rng(cfg.seed);
    Logger     logger([](const LogEntry&) {});  // silent during calibration
    EventBus   bus;

    MatchingEngine      matching(cfg.ticker, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger      ledger;
    Clearing            clearing(ledger, cfg.ticker);
    MarketDataPublisher publisher(matching.book());
    publisher.set_initial_mid(static_cast<Price>(cfg.initial_price_ticks));

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

    AgentFactory factory(cfg.population, rng, cfg.ticker, &bus, &ledger);
    auto owned = factory.create_all();
    std::vector<IAgent*> ptrs;
    ptrs.reserve(owned.size());
    for (auto& a : owned) ptrs.push_back(a.get());

    AgentRunner runner(ptrs, rng.global_seed(), &ledger, cfg.ticker);
    RiskGate    risk_gate(ledger, cfg.ticker);

    SimulationLoop::Config sim_cfg;
    sim_cfg.max_ticks        = cfg.max_ticks;
    sim_cfg.publish_interval = cfg.publish_interval_ticks;
    sim_cfg.ticker           = cfg.ticker;
    sim_cfg.tick_size        = cfg.tick_size;

    BinaryLogWriter log_writer(
        log_path, cfg.seed, cfg.max_ticks,
        static_cast<uint64_t>(cfg.publish_interval_ticks), cfg.ticker);

    SnapshotBuffer snap_buf;
    SimulationLoop sim_loop(sim_cfg, fundamental, news, runner, risk_gate,
                            matching, clearing, ledger, publisher, logger);
    sim_loop.set_event_bus(&bus);
    sim_loop.set_log_writer(&log_writer);
    sim_loop.run(snap_buf);
    log_writer.flush(true);

    return Report::run(log_path);
}

// ── Aggregate N reports ───────────────────────────────────────────────────────

static void print_ensemble(const std::vector<AnalysisReport>& reports) {
    if (reports.empty()) return;
    int n_facts = static_cast<int>(reports[0].results.size());
    int n       = static_cast<int>(reports.size());

    std::printf("\nEnsemble Report (%d seeds)\n", n);
    std::printf("==========================\n");
    std::printf("| # | Test                              | Mean      | Std       |Threshold| Pass%% |\n");
    std::printf("|---|-----------------------------------|-----------|-----------|---------|-------|\n");

    for (int f = 0; f < n_facts; ++f) {
        std::vector<double> vals;
        int pass_count = 0;
        for (const auto& rep : reports) {
            if (f < (int)rep.results.size()) {
                vals.push_back(rep.results[f].value);
                if (rep.results[f].passed) ++pass_count;
            }
        }
        double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / n;
        double var  = 0.0;
        for (double v : vals) var += (v - mean) * (v - mean);
        double std  = (n > 1) ? std::sqrt(var / n) : 0.0;
        double thr  = reports[0].results[f].threshold;
        double pct  = 100.0 * pass_count / n;

        char row[300];
        std::snprintf(row, sizeof(row),
            "| %d | %-33s | %9.4f | %9.4f | %7.4f | %5.1f%% |\n",
            f + 1,
            reports[0].results[f].name.c_str(),
            mean, std, thr, pct);
        std::printf("%s", row);
    }

    // Extra diagnostics ensemble
    auto avg = [&](auto fn) {
        double s = 0.0;
        for (const auto& r : reports) s += fn(r);
        return s / n;
    };
    std::printf("\nExtra diagnostics (mean across seeds):\n");
    std::printf("  Ljung-Box Q(10)   = %.2f\n",  avg([](const AnalysisReport& r){ return r.ljung_box_q; }));
    std::printf("  Hill α multi-k    = %.3f\n",  avg([](const AnalysisReport& r){ return r.hill_alpha_mean; }));
    std::printf("  Trade sign ACF(1) = %.4f\n",  avg([](const AnalysisReport& r){ return r.trade_sign_acf1; }));
    std::printf("  Vol-volume corr   = %.4f\n",  avg([](const AnalysisReport& r){ return r.vol_vol_corr; }));

    // Overall pass rate
    int total_pass = 0, total_facts = 0;
    for (const auto& rep : reports) {
        total_pass  += rep.passed_count;
        total_facts += rep.total_count;
    }
    std::printf("\nOverall: %.1f%% facts passed across all seeds (%.1f/%.1f avg)\n",
                100.0 * total_pass / total_facts,
                static_cast<double>(total_pass) / n,
                static_cast<double>(total_facts) / n);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int      n_seeds  = 10;
    uint64_t duration = 50'000;
    std::string outdir   = "logs/calibrate/";
    SimulationConfig base_cfg;
    base_cfg.max_ticks = duration;

    for (int i = 1; i < argc - 1; ++i) {
        std::string key = argv[i];
        if (key == "--seeds")    n_seeds               = std::stoi(argv[i+1]);
        if (key == "--duration") base_cfg.max_ticks    = std::stoull(argv[i+1]);
        if (key == "--outdir")   outdir                = argv[i+1];
        if (key == "--ticker")   base_cfg.ticker       = argv[i+1];
        if (key == "--process") {
            std::string p = argv[i+1];
            if      (p == "gbm")  base_cfg.fundamental.type = FundamentalProcessType::GBM;
            else if (p == "ou")   base_cfg.fundamental.type = FundamentalProcessType::OU;
            else if (p == "jump") base_cfg.fundamental.type = FundamentalProcessType::JumpDiffusion;
            else                  base_cfg.fundamental.type = FundamentalProcessType::RegimeSwitching;
        }
        if (key == "--gaussian") base_cfg.fundamental.gaussian_innovations = true;
    }

    std::filesystem::create_directories(outdir);

    std::fprintf(stderr, "[INFO] sim_calibrate: seeds=%d  ticks=%llu  outdir=%s\n",
                 n_seeds, (unsigned long long)base_cfg.max_ticks, outdir.c_str());

    std::vector<AnalysisReport> reports;
    reports.reserve(n_seeds);

    for (int s = 0; s < n_seeds; ++s) {
        std::string log_path = outdir + "seed_" + std::to_string(s) + ".bin";
        std::fprintf(stderr, "  [%d/%d] seed=%d ...\n", s + 1, n_seeds, s);
        reports.push_back(run_one(base_cfg, static_cast<uint64_t>(s), log_path));
        auto& rep = reports.back();
        std::fprintf(stderr, "         %d/%d facts passed\n",
                     rep.passed_count, rep.total_count);
    }

    print_ensemble(reports);
    return 0;
}
