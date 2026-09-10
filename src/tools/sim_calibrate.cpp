// sim_calibrate [--seeds N] [--duration T] [--outdir path] [--ticker T]
//               [--process P] [--gaussian] [--save path]
// Ensemble calibration harness: runs N seeds, aggregates mean ± std for each
// stylized-fact metric and for the price-discovery diagnostics, and prints a
// summary table.  A single seed can pass every fact by accident; the ensemble
// is what decides whether a behaviour is a property of the model or of a path.
//
// --gaussian replaces the Student-t innovations of the fundamental with normal
// ones.  It is the emergence test: fat tails that survive a Gaussian driver are
// produced by the market's own microstructure, and ones that vanish were merely
// inherited from the input distribution.
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
#include <cstdarg>
#include <cstring>
#include <map>
#include <numeric>
#include <string>
#include <vector>

// ── Run one seed and return its report ───────────────────────────────────────

// One seed's result: the report plus the ecology diagnostic that no report can
// see, since it lives in the runner rather than in the log.
struct SeedResult {
    AnalysisReport report;
    double         max_at_limit_frac = 0.0;  // worst agent type, fraction of ticks
    std::string    max_at_limit_type;
};

static SeedResult run_one(const SimulationConfig& base_cfg,
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

    // Market-maker ids let the report separate MM-vs-MM churn from real trading.
    std::vector<AgentId> mm_ids;
    for (IAgent* a : ptrs)
        if (std::strcmp(a->type(), "MarketMakerAS") == 0) mm_ids.push_back(a->id());

    SeedResult out;
    out.report = Report::run(log_path, mm_ids);

    // Worst agent type by time spent pinned at its position limit.  A type that
    // lives at the limit has stopped choosing and is just a wall.
    struct Acc { int n = 0; uint64_t at_limit = 0; };
    std::map<std::string, Acc> by_type;
    const auto& at_limit = runner.ticks_at_limit();
    for (IAgent* a : ptrs) {
        Acc& acc = by_type[a->type()];
        ++acc.n;
        auto it = at_limit.find(a->id());
        if (it != at_limit.end()) acc.at_limit += it->second;
    }
    const double ticks = runner.ticks_run() > 0
        ? static_cast<double>(runner.ticks_run()) : 1.0;
    for (const auto& [name, acc] : by_type) {
        const double frac = static_cast<double>(acc.at_limit) / (ticks * acc.n);
        if (frac > out.max_at_limit_frac) {
            out.max_at_limit_frac = frac;
            out.max_at_limit_type = name;
        }
    }
    return out;
}

// ── Aggregate N seeds ────────────────────────────────────────────────────────

namespace {

struct Stat { double mean = 0.0, sd = 0.0; };

Stat summarize(const std::vector<double>& v) {
    Stat st;
    if (v.empty()) return st;
    const double n = static_cast<double>(v.size());
    st.mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
    double var = 0.0;
    for (double x : v) var += (x - st.mean) * (x - st.mean);
    st.sd = (v.size() > 1) ? std::sqrt(var / n) : 0.0;
    return st;
}

void append(std::string& out, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

// mean ± sd of one double field of the report, across seeds.
template <typename Fn>
Stat field(const std::vector<SeedResult>& rs, Fn fn) {
    std::vector<double> v;
    v.reserve(rs.size());
    for (const auto& r : rs) v.push_back(fn(r));
    return summarize(v);
}

// Fraction of seeds for which pred holds.
template <typename Fn>
double frac_where(const std::vector<SeedResult>& rs, Fn pred) {
    if (rs.empty()) return 0.0;
    int k = 0;
    for (const auto& r : rs) if (pred(r)) ++k;
    return static_cast<double>(k) / static_cast<double>(rs.size());
}

}  // namespace

static std::string ensemble_text(const std::vector<SeedResult>& rs,
                                 const SimulationConfig& cfg) {
    std::string out;
    if (rs.empty()) return out;
    const int n       = static_cast<int>(rs.size());
    const int n_facts = static_cast<int>(rs[0].report.results.size());

    append(out, "Ensemble Report (%d seeds x %llu ticks)\n", n,
           (unsigned long long)cfg.max_ticks);
    append(out, "=======================================\n");
    append(out, "Fundamental: %s   innovations: %s   sigma_annual = %.2f\n",
           rs[0].report.pd.has_fundamental ? "logged" : "absent",
           cfg.fundamental.gaussian_innovations ? "Gaussian" : "Student-t",
           cfg.fundamental.sigma_annual);
    append(out, "News: %.1f/day, impact scale %.4f\n\n",
           cfg.news.events_per_day, cfg.news.impact_scale);

    append(out, "| # | Test                              | Mean      | Std       |Threshold| Pass%% |\n");
    append(out, "|---|-----------------------------------|-----------|-----------|---------|-------|\n");
    for (int f = 0; f < n_facts; ++f) {
        std::vector<double> vals;
        int pass_count = 0;
        for (const auto& r : rs) {
            if (f >= (int)r.report.results.size()) continue;
            vals.push_back(r.report.results[f].value);
            if (r.report.results[f].passed) ++pass_count;
        }
        const Stat st = summarize(vals);
        append(out, "| %d | %-33s | %9.4f | %9.4f | %7.4f | %5.1f%% |\n",
               f + 1, rs[0].report.results[f].name.c_str(),
               st.mean, st.sd, rs[0].report.results[f].threshold,
               100.0 * pass_count / n);
    }

    // ── Price discovery ─────────────────────────────────────────────────────
    // These are prior to the stylized facts: a price that does not track the
    // fundamental can still show fat tails and clustered volatility, and would
    // still be worthless.
    append(out, "\nPrice discovery (mean +- std across seeds)\n");
    append(out, "-----------------------------------------\n");
    struct PdRow {
        const char* label;
        double (*get)(const SeedResult&);
        char   cmp;        // '>' or '<'; ' ' when the row has no target
        double target;
    };
    const PdRow pd_rows[] = {
        {"corr(rV, r_mid) h=1  ", [](const SeedResult& r){ return r.report.pd.corr_h1;   }, ' ', 0.0},
        {"corr(rV, r_mid) h=5  ", [](const SeedResult& r){ return r.report.pd.corr_h5;   }, ' ', 0.0},
        {"corr(rV, r_mid) h=20 ", [](const SeedResult& r){ return r.report.pd.corr_h20;  }, '>', 0.30},
        {"corr(rV, r_mid) h=100", [](const SeedResult& r){ return r.report.pd.corr_h100; }, '>', 0.50},
        {"std(log(mid/V))      ", [](const SeedResult& r){ return r.report.pd.gap_std;   }, '<', 0.01},
        {"mean(log(mid/V))     ", [](const SeedResult& r){ return r.report.pd.gap_mean;  }, ' ', 0.0},
        {"gap half-life (ticks)", [](const SeedResult& r){ return r.report.pd.gap_half_life; }, '<', 50.0},
        {"two-sided book       ", [](const SeedResult& r){ return r.report.pd.two_sided_frac; }, '>', 0.99},
        {"zero returns         ", [](const SeedResult& r){ return r.report.pd.zero_ret_frac; }, ' ', 0.0},
        {"MM-vs-MM volume      ", [](const SeedResult& r){ return r.report.pd.mm_self_trade_frac; }, '<', 0.05},
        {"worst type at limit  ", [](const SeedResult& r){ return r.max_at_limit_frac; }, '<', 0.05},
    };
    // Mean and standard deviation hide a single pathological path, and a gap
    // half-life estimated from an AR(1) fit has a heavy right tail, so the
    // median and the count of seeds meeting the target are reported too.
    for (const PdRow& row : pd_rows) {
        std::vector<double> v;
        v.reserve(rs.size());
        for (const auto& r : rs) v.push_back(row.get(r));
        const Stat st = summarize(v);
        std::vector<double> sorted = v;
        std::sort(sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];

        if (row.cmp == ' ') {
            append(out, "  %s = %10.4f +- %-9.4f  median %10.4f\n",
                   row.label, st.mean, st.sd, med);
        } else {
            const int k = (int)std::count_if(v.begin(), v.end(), [&](double x){
                return row.cmp == '>' ? (x > row.target) : (x < row.target); });
            append(out, "  %s = %10.4f +- %-9.4f  median %10.4f   %c %.2f on %2d/%d seeds\n",
                   row.label, st.mean, st.sd, med, row.cmp, row.target,
                   k, (int)rs.size());
        }
    }

    // Which agent type is the worst offender, and how often it is that one.
    std::map<std::string, int> worst_counts;
    for (const auto& r : rs)
        if (!r.max_at_limit_type.empty()) ++worst_counts[r.max_at_limit_type];
    out += "  worst type at limit, by seed:";
    if (worst_counts.empty()) {
        // No seed had any type spend a single tick near its limit.
        out += " none";
    } else {
        for (const auto& [name, k] : worst_counts)
            append(out, " %s x%d", name.c_str(), k);
    }
    out += "\n";

    // ── Extra diagnostics ───────────────────────────────────────────────────
    append(out, "\nExtra diagnostics (mean +- std across seeds)\n");
    append(out, "-------------------------------------------\n");
    const Stat lb  = field(rs, [](const SeedResult& r){ return r.report.ljung_box_q; });
    const Stat hil = field(rs, [](const SeedResult& r){ return r.report.hill_alpha_mean; });
    const Stat tsa = field(rs, [](const SeedResult& r){ return r.report.trade_sign_acf1; });
    const Stat vvc = field(rs, [](const SeedResult& r){ return r.report.vol_vol_corr; });
    const Stat trd = field(rs, [](const SeedResult& r){ return (double)r.report.n_trades; });
    append(out, "  Ljung-Box Q(10)   = %10.2f +- %.2f\n",  lb.mean,  lb.sd);
    append(out, "  Hill alpha        = %10.3f +- %.3f\n",  hil.mean, hil.sd);
    append(out, "  Trade sign ACF(1) = %10.4f +- %.4f   (target 0.10 .. 0.40)\n", tsa.mean, tsa.sd);
    append(out, "  Vol-volume corr   = %10.4f +- %.4f\n",  vvc.mean, vvc.sd);
    append(out, "  Trades per run    = %10.0f +- %.0f\n",  trd.mean, trd.sd);

    // ── Per-seed fact counts and the headline pass rate ─────────────────────
    append(out, "\nPer-seed facts passed:");
    for (const auto& r : rs)
        append(out, " %d/%d", r.report.passed_count, r.report.total_count);
    out += "\n";

    int total_pass = 0, total_facts = 0;
    for (const auto& r : rs) {
        total_pass  += r.report.passed_count;
        total_facts += r.report.total_count;
    }
    append(out, "\nOverall: %.1f%% of facts passed across all seeds (%.1f/%.1f avg)\n",
           100.0 * total_pass / total_facts,
           static_cast<double>(total_pass) / n,
           static_cast<double>(total_facts) / n);

    // Per-fact robustness: a fact that passes on 10/10 seeds is a property of
    // the model; one that passes on 5/10 is a property of the path.
    append(out, "\nRobustness (seeds passing each fact):\n");
    for (int f = 0; f < n_facts; ++f) {
        const int k = (int)std::count_if(rs.begin(), rs.end(),
            [&](const SeedResult& r){
                return f < (int)r.report.results.size() && r.report.results[f].passed; });
        append(out, "  %-33s %2d/%d\n", rs[0].report.results[f].name.c_str(), k, n);
    }

    append(out, "\nEcology: %.0f%% of seeds keep every agent type under 5%% of ticks at its limit.\n",
           100.0 * frac_where(rs, [](const SeedResult& r){ return r.max_at_limit_frac < 0.05; }));

    return out;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int         n_seeds = 10;
    std::string outdir  = "logs/calibrate/";
    std::string save_path;
    SimulationConfig base_cfg;
    base_cfg.max_ticks = 50'000;

    // The loop runs to argc so that a valueless flag in last position is still
    // seen; flags that take a value check for it before reading.
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const bool has_val = (i + 1 < argc);
        if (key == "--gaussian") { base_cfg.fundamental.gaussian_innovations = true; continue; }
        if (!has_val) continue;
        if (key == "--seeds")    n_seeds            = std::stoi(argv[i+1]);
        if (key == "--duration") base_cfg.max_ticks = std::stoull(argv[i+1]);
        if (key == "--outdir")   outdir             = argv[i+1];
        if (key == "--save")     save_path          = argv[i+1];
        if (key == "--ticker")   base_cfg.ticker    = argv[i+1];
        if (key == "--sigma-annual") base_cfg.fundamental.sigma_annual = std::stod(argv[i+1]);
        if (key == "--process") {
            const std::string p = argv[i+1];
            if      (p == "gbm")  base_cfg.fundamental.type = FundamentalProcessType::GBM;
            else if (p == "ou")   base_cfg.fundamental.type = FundamentalProcessType::OU;
            else if (p == "jump") base_cfg.fundamental.type = FundamentalProcessType::JumpDiffusion;
            else                  base_cfg.fundamental.type = FundamentalProcessType::RegimeSwitching;
        }
    }

    std::filesystem::create_directories(outdir);

    std::fprintf(stderr, "[INFO] sim_calibrate: seeds=%d  ticks=%llu  outdir=%s  innovations=%s\n",
                 n_seeds, (unsigned long long)base_cfg.max_ticks, outdir.c_str(),
                 base_cfg.fundamental.gaussian_innovations ? "gaussian" : "student-t");

    std::vector<SeedResult> results;
    results.reserve(n_seeds);

    for (int s = 0; s < n_seeds; ++s) {
        const std::string log_path = outdir + "seed_" + std::to_string(s) + ".bin";
        std::fprintf(stderr, "  [%d/%d] seed=%d ...\n", s + 1, n_seeds, s);
        results.push_back(run_one(base_cfg, static_cast<uint64_t>(s), log_path));
        const SeedResult& r = results.back();
        std::fprintf(stderr, "         %d/%d facts   corr_h100=%.3f  gap_std=%.4f  worst-at-limit=%.2f%% (%s)\n",
                     r.report.passed_count, r.report.total_count,
                     r.report.pd.corr_h100, r.report.pd.gap_std,
                     100.0 * r.max_at_limit_frac, r.max_at_limit_type.c_str());
    }

    const std::string text = ensemble_text(results, base_cfg);
    std::printf("\n%s", text.c_str());

    if (!save_path.empty()) {
        std::filesystem::create_directories(
            std::filesystem::path(save_path).parent_path());
        if (FILE* f = std::fopen(save_path.c_str(), "w")) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
            std::fprintf(stderr, "[INFO] ensemble saved to %s\n", save_path.c_str());
        } else {
            std::fprintf(stderr, "[ERROR] could not write %s\n", save_path.c_str());
            return 1;
        }
    }
    return 0;
}
