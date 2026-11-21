// sim_calibrate [--seed-set cal|val | --seeds N [--seed-start S]]
//               [--duration T | --days D] [--jobs J] [--keep-logs]
//               [--outdir path] [--csv path] [--baseline path.csv]
//               [--ticker T] [--process P] [--gaussian] [--save path]
//               [--news-noise S] [--news-blind] [--taker-fee BP] [--maker-rebate BP]
//               [--set key=value ...]
// Ensemble calibration harness: runs N seeds and reports every metric as the
// ensemble mean ± standard error, tagged CAL / VAL / diag by the frozen
// protocol in analysis/Protocol.h.  A single seed can pass every fact by
// accident; the ensemble is what decides whether a behaviour is a property of
// the model or of a path.
//
// --seed-set picks the protocol's seed range (CAL 1..10, VAL 101..120).  Every
// run writes one CSV line per (seed, metric) to --csv (default
// <outdir>/per_seed.csv).  --baseline <csv> pairs the current run with an
// earlier one seed by seed and prints Δ ± SE(Δ) with a verdict: a change is an
// improvement or a regression only when |Δ| > 2·SE.
//
// --gaussian replaces the Student-t innovations of the fundamental with normal
// ones.  It is the emergence test: fat tails that survive a Gaussian driver are
// produced by the market's own microstructure, and ones that vanish were merely
// inherited from the input distribution.
//
// By default the series the report needs are collected in memory and no .bin
// is written; --keep-logs also writes seed_<s>.bin to --outdir.  --jobs runs
// seeds on parallel threads: each seed owns every object it touches and all
// randomness comes from its own RngService, so the result does not depend on
// J (checked by running --jobs 1 against --jobs 8).
//
// --taker-fee / --maker-rebate charge bp of notional per fill.  No agent sees
// the fee, so the dynamics are unchanged; the P&L table shows who pays it and
// checks that Σ net P&L = −Σ fees exactly.
//
// --set overrides one population parameter for an ablation (see apply_set).
// mom.qty / mr.qty replace the order size after the parameter draws, so 0
// silences a cohort without moving any other agent's RNG stream; mm.count
// and the other .count keys change how many agents are drawn and therefore
// every draw after them.  mm.impact_q_ref=0 restores Kyle's linear rule.
//
// Besides the report and the CSV, each run writes <dir of --csv>/daily.csv:
// log(mid/V) at every session close and the share of the session with a
// two-sided book.  A seed with a session below 50% two-sided is a collapse;
// its facts are reported apart and stay out of the ensemble means.
#include "analysis/Protocol.h"
#include "analysis/Report.h"
#include "analysis/SeriesCollector.h"
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
#include "agents/StopLossCluster.h"
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
#include <atomic>
#include <mutex>
#include <optional>
#include <functional>
#include <limits>
#include <pthread.h>

// ── Run one seed and return its report ───────────────────────────────────────

// One seed's result: the report plus the ecology diagnostic that no report can
// see, since it lives in the runner rather than in the log.
struct SeedResult {
    AnalysisReport report;
    double         max_at_limit_frac = 0.0;  // worst agent type, fraction of ticks
    std::string    max_at_limit_type;
    // Mark-to-market P&L (realized + unrealized at the final mid), ticks·lots.
    // Kyle's prediction is the sign: informed > 0, makers ~0, noise < 0.
    double         informed_pnl_mtm = 0.0;
    double         mm_pnl_mtm       = 0.0;

    // Who pays whom.  Gross P&L is realized + unrealized at the final mid, in
    // ticks·lots; fees are what the class paid (negative: rebates earned).
    struct ClassPnl { int agents = 0; double gross = 0, fees = 0, net = 0; uint64_t fills = 0; };
    std::map<std::string, ClassPnl> by_class;   // a->type(), plus "Alocação inicial"
    ZeroSumCheck zero_sum;
    int          stops_fired = 0;
    double       usd_per_tick_lot = 0.0;   // tick_size × lot_size

    // Long horizon, one entry per session: log(mid/V) at the session's last
    // snapshot and the share of the session's ticks with a two-sided book.
    // A session with less than kCollapseTwoSided two-sided is a collapse:
    // the "market" there is a one-sided queue, and its facts are not facts.
    std::vector<double> day_gap, day_two_sided;
    int collapse_day = -1;   // first collapsed session, -1 = none
    bool collapsed() const { return collapse_day >= 0; }
};

static constexpr double kCollapseTwoSided = 0.5;
static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Every class the factory builds, in a fixed order for the table and the CSV.
static const char* const kClasses[] = {
    "MarketMakerAS", "InformedTraderKyle", "NoiseTrader", "InstitutionalExecutor",
    "MomentumTrader", "MeanReverterOU", "ValueInvestor", "StopLossCluster",
    "NewsReactor", "LimitOrderTrader", "Alocação inicial",
};

// log_path empty → memory only.
static SeedResult run_one(const SimulationConfig& base_cfg,
                          uint64_t seed,
                          const std::string& log_path) {
    SimulationConfig cfg = base_cfg;
    cfg.seed             = seed;
    cfg.log_output_path  = log_path;

    RngService rng(cfg.seed);
    Logger     logger([](const LogEntry&) {});  // silent during calibration
    EventBus   bus;

    MatchingEngine      matching(cfg.ticker, FeeModel{cfg.taker_fee_bp, cfg.maker_rebate_bp}, STPMode::CancelBoth, &rng);
    PositionLedger      ledger;
    Clearing            clearing(ledger, cfg.ticker);
    MarketDataPublisher publisher(matching.book());
    publisher.set_initial_mid(static_cast<Price>(cfg.initial_price_ticks));

    // Announcements draw from the same variance budget as the diffusion.
    cfg.fundamental.news_var_per_tick = cfg.news.variance_per_tick(cfg.time);
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

    // The session shape has to speak the same clock as everything else.
    cfg.population.intraday.ticks_per_day = cfg.time.ticks_per_day();
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

    // Market-maker ids let the report separate MM-vs-MM churn from real trading.
    std::vector<AgentId> mm_ids;
    for (IAgent* a : ptrs)
        if (std::strcmp(a->type(), "MarketMakerAS") == 0) mm_ids.push_back(a->id());
    publisher.set_mm_ids(mm_ids);

    SeriesCollector collector(mm_ids);
    std::optional<BinaryLogWriter> log_writer;
    std::optional<TeeLogSink>      tee;
    ILogSink* sink = &collector;
    if (!log_path.empty()) {
        log_writer.emplace(log_path, cfg.seed, cfg.max_ticks,
                           static_cast<uint64_t>(cfg.publish_interval_ticks), cfg.ticker);
        tee.emplace(*log_writer, collector);
        sink = &*tee;
    }

    SnapshotBuffer snap_buf;
    SimulationLoop sim_loop(sim_cfg, fundamental, news, runner, risk_gate,
                            matching, clearing, ledger, publisher, logger);
    sim_loop.set_event_bus(&bus);
    sim_loop.set_log_writer(sink);
    sim_loop.run(snap_buf);
    if (log_writer) log_writer->flush(true);

    SeedResult out;
    const LogData d = collector.finish();
    out.report = Report::compute(d, cfg.time);

    {
        const uint64_t tpd    = cfg.time.ticks_per_day();
        const size_t   n_days = static_cast<size_t>((cfg.max_ticks + tpd - 1) / tpd);
        out.day_gap.assign(n_days, kNaN);
        out.day_two_sided.assign(n_days, 0.0);
        std::vector<double> ticks_in_day(n_days, 0.0);
        for (size_t day = 0; day < n_days; ++day)
            ticks_in_day[day] = static_cast<double>(
                std::min<uint64_t>(tpd, cfg.max_ticks - day * tpd));
        for (size_t i = 0; i < d.mid_prices.size(); ++i) {
            const size_t day = static_cast<size_t>(d.price_ticks[i] / tpd);
            if (day >= n_days) continue;
            if (d.spreads[i] > 0) out.day_two_sided[day] += 1.0;
            if (i < d.fundamental.size() && d.fundamental[i] > 0.0)
                out.day_gap[day] = std::log(d.mid_prices[i] / d.fundamental[i]);
        }
        for (size_t day = 0; day < n_days; ++day) {
            out.day_two_sided[day] /= std::max(1.0, ticks_in_day[day]);
            if (out.collapse_day < 0 && out.day_two_sided[day] < kCollapseTwoSided)
                out.collapse_day = static_cast<int>(day);
        }
    }

    {
        MarketSnapshot last_snap;
        const Price mark = snap_buf.get_latest(last_snap) ? last_snap.mid_price
                                                           : static_cast<Price>(cfg.initial_price_ticks);
        const auto book = [&](const std::string& cls, AgentId id) {
            auto& c = out.by_class[cls];
            ++c.agents;
            c.gross += static_cast<double>(ledger.realized_pnl(id, cfg.ticker)
                                         + ledger.unrealized_pnl(id, cfg.ticker, mark));
            c.fees  += static_cast<double>(ledger.fees_net(id, cfg.ticker));
            c.net   += static_cast<double>(ledger.net_qty(id, cfg.ticker));
            if (const PositionRecord* rec = ledger.get(id, cfg.ticker)) c.fills += rec->fills;
        };
        for (IAgent* a : ptrs) {
            book(a->type(), a->id());
            if (const auto* sl = dynamic_cast<const StopLossCluster*>(a))
                out.stops_fired += sl->stops_fired();
        }
        if (ledger.get(kAllocationAccount, cfg.ticker)) {
            book("Alocação inicial", kAllocationAccount);
            out.by_class["Alocação inicial"].agents = 0;   // an account, not an agent
        }
        out.informed_pnl_mtm = out.by_class["InformedTraderKyle"].gross;
        out.mm_pnl_mtm       = out.by_class["MarketMakerAS"].gross;
        out.zero_sum = ledger.zero_sum(cfg.ticker, mark);
        out.usd_per_tick_lot = cfg.tick_size * static_cast<double>(cfg.lot_size);
    }

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


// Mean, standard error of the mean and median over the finite values; a
// metric that a seed cannot measure (NaN) is left out, and n says so.
struct Stat { double mean = kNaN, se = kNaN, median = kNaN; int n = 0; };

Stat summarize(const std::vector<double>& v_in) {
    std::vector<double> v;
    for (double x : v_in) if (std::isfinite(x)) v.push_back(x);
    Stat st;
    st.n = static_cast<int>(v.size());
    if (v.empty()) return st;
    const double n = static_cast<double>(v.size());
    st.mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
    double ss = 0.0;
    for (double x : v) ss += (x - st.mean) * (x - st.mean);
    st.se = (v.size() > 1) ? std::sqrt(ss / (n - 1.0)) / std::sqrt(n) : kNaN;
    std::sort(v.begin(), v.end());
    st.median = (v.size() % 2) ? v[v.size() / 2]
                               : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
    return st;
}

void append(std::string& out, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

// ── The metric table ────────────────────────────────────────────────────────
// Every number the ensemble reports, the per-seed CSV stores and --baseline
// compares.  cmp:
//   '>' / '<'  target lo, the seed passes above / below it
//   'r'        target range [lo, hi]
//   't'        closeness to lo (no pass/fail, lower |x − lo| is better)
//   '+' / '-'  higher / lower is better, no target
//   ' '        no preferred direction
struct Metric {
    std::string key;
    std::string label;
    protocol::Set set = protocol::Set::Diag;
    char   cmp = ' ';
    double lo = 0.0, hi = 0.0;
    std::function<double(const SeedResult&)> get;
    bool   in_text = true;   // false: CSV and --baseline only (shown elsewhere in the text)
    // false: the target is a sign test, not a preference.  More maker P&L is
    // not "better" — the surplus has to come from someone.
    bool   directional = true;
};

// A v2 fact without enough data returns early, INCONCLUSIVO with value and
// interval untouched at zero; that is "not measured", not a measured zero.
double v2_value(const SeedResult& r, const std::string& id) {
    for (const auto& f : r.report.v2.facts) {
        if (f.id != id) continue;
        const bool unmeasured = f.status == sfv2::Status::Inconclusive
                             && f.value == 0.0 && f.ci_lo == 0.0 && f.ci_hi == 0.0;
        return unmeasured ? kNaN : f.value;
    }
    return kNaN;
}
const sfv2::FactResult* v2_fact(const SeedResult& r, const std::string& id) {
    for (const auto& f : r.report.v2.facts) if (f.id == id) return &f;
    return nullptr;
}

std::vector<Metric> metric_table() {
    using protocol::Set;
    std::vector<Metric> m;
    const auto add = [&](std::string key, std::string label, char cmp, double lo, double hi,
                         std::function<double(const SeedResult&)> get) {
        Metric x;
        x.key = std::move(key); x.label = std::move(label);
        x.cmp = cmp; x.lo = lo; x.hi = hi; x.get = std::move(get);
        m.push_back(std::move(x));
    };
    const auto pd_or_nan = [](bool ok, double v) { return ok ? v : kNaN; };

    // CAL
    add("cal.vol_ann_1m",    "vol anual do mid (1 min)",   't', protocol::kVolAnnualTarget, 0,
        [](const SeedResult& r){ return r.report.realism.ann_vol_mid_1m; });
    add("cal.spread_bp",     "spread médio (bp)",          ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.mean_spread_bp; });
    add("cal.u_volume",      "razão U de volume",          '>', 1.30, 0,
        [](const SeedResult& r){ return r.report.realism.intraday_u_ratio > 0
                                        ? r.report.realism.intraday_u_ratio : kNaN; });
    add("cal.mm_top_share",  "share do maior maker",       '<', 0.50, 0,
        [](const SeedResult& r){ return r.report.realism.mm_top_share; });
    add("cal.gap_half_life", "meia-vida do gap (ticks)",   '<', 50.0, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental,
                                                           r.report.pd.gap_half_life); });

    // VAL
    for (int k = 1; k <= 10; ++k) {
        const std::string id = "v2." + std::to_string(k);
        add("val." + id, id + " (valor)", ' ', 0, 0,
            [id](const SeedResult& r){ return v2_value(r, id); });
    }
    add("val.v2_pass",       "bateria v2: fatos PASSA",    '+', 0, 0,
        [](const SeedResult& r){ return static_cast<double>(r.report.v2.pass); });
    add("val.delta_meta",    "δ de metaordem",             'r', protocol::kDeltaLo, protocol::kDeltaHi,
        [](const SeedResult& r){ return r.report.metaorder.delta_valid
                                        ? r.report.metaorder.delta : kNaN; });
    add("val.pd_diff_h20",   "corr(r_mid, r_difusão) h=20",  '>', 0.30, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_components,
                                                           r.report.pd.corr_diff[2]); });
    add("val.pd_diff_h100",  "corr(r_mid, r_difusão) h=100", '>', 0.50, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_components,
                                                           r.report.pd.corr_diff[3]); });

    // Diagnostics: reported and stored, but neither tuned to nor judged by.
    add("diag.legacy_pass",  "bateria legada: fatos passados", '+', 0, 0,
        [](const SeedResult& r){ return static_cast<double>(r.report.passed_count); });
    // Already in the legacy table of the text; kept for the CSV and --baseline.
    for (int k = 1; k <= 8; ++k) {
        add("diag.legacy." + std::to_string(k), "Fato legado " + std::to_string(k) + " (valor)",
            ' ', 0, 0, [k](const SeedResult& r){
                return k <= (int)r.report.results.size() ? r.report.results[k - 1].value : kNaN; });
        m.back().in_text = false;
    }
    add("diag.corr_h1",      "corr(r_V, r_mid) h=1",       ' ', 0, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental, r.report.pd.corr_h1); });
    add("diag.corr_h20",     "corr(r_V, r_mid) h=20",      '>', 0.30, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental, r.report.pd.corr_h20); });
    add("diag.corr_h100",    "corr(r_V, r_mid) h=100",     '>', 0.50, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental, r.report.pd.corr_h100); });
    add("diag.pd_news_h100", "corr(r_mid, r_notícias) h=100", ' ', 0, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_components, r.report.pd.corr_news[3]); });
    add("diag.corr_h100_ex", "corr h=100 longe das notícias", '>', 0.50, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_components,
                                                           r.report.pd.corr_h100_ex_news); });
    add("diag.gap_std",      "dp(log(mid/V))",             '<', 0.01, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental, r.report.pd.gap_std); });
    add("diag.gap_mean",     "média(log(mid/V))",          ' ', 0, 0,
        [pd_or_nan](const SeedResult& r){ return pd_or_nan(r.report.pd.has_fundamental, r.report.pd.gap_mean); });
    add("diag.non_mm_top",   "topo do book fora dos makers", ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.non_mm_top_depth_frac >= 0.0
                                        ? r.report.realism.non_mm_top_depth_frac : kNaN; });
    add("diag.two_sided",    "book com dois lados",        '>', 0.99, 0,
        [](const SeedResult& r){ return r.report.pd.two_sided_frac; });
    add("diag.zero_ret",     "retornos zero (1 s)",        ' ', 0, 0,
        [](const SeedResult& r){ return r.report.pd.zero_ret_frac; });
    add("diag.mm_self",      "volume maker contra maker",  '<', 0.05, 0,
        [](const SeedResult& r){ return r.report.pd.mm_self_trade_frac; });
    add("diag.at_limit",     "pior tipo no limite",        '<', 0.05, 0,
        [](const SeedResult& r){ return r.max_at_limit_frac; });
    add("diag.vol_fund_1m",  "vol anual de V (1 min)",     ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.ann_vol_fund_1m; });
    add("diag.signature",    "assinatura de vol 1 s / 1 min", ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.signature_ratio; });
    add("diag.rule_exponent","expoente por tick (eco da regra)", ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.impact_exponent; });
    add("diag.y_meta",       "prefator Y de metaordem",    ' ', 0, 0,
        [](const SeedResult& r){ return r.report.metaorder.n > 0 ? r.report.metaorder.y_prefactor : kNaN; });
    add("diag.perm_temp",    "impacto permanente/temporário", ' ', 0, 0,
        [](const SeedResult& r){ return r.report.metaorder.n_perm > 0 ? r.report.metaorder.perm_over_temp : kNaN; });
    add("diag.u_absr",       "razão U de |r| (1 min)",     ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.intraday_u_absr > 0 ? r.report.realism.intraday_u_absr : kNaN; });
    add("diag.u_spread",     "razão U do spread",          ' ', 0, 0,
        [](const SeedResult& r){ return r.report.realism.intraday_u_spread > 0 ? r.report.realism.intraday_u_spread : kNaN; });
    add("diag.flash",        "flash crashes",              '<', 2.0, 0,
        [](const SeedResult& r){ return static_cast<double>(r.report.flash_crashes); });
    add("diag.pnl_informed", "P&L MtM dos informados",     '>', 0.0, 0,
        [](const SeedResult& r){ return r.informed_pnl_mtm; });
    m.back().directional = false;
    add("diag.pnl_mm",       "P&L MtM dos makers",         '>', 0.0, 0,
        [](const SeedResult& r){ return r.mm_pnl_mtm; });
    m.back().directional = false;
    // P&L by class, in US$ (ticks·lots × tick_size × lot_size).  Gross of fees.
    for (const char* cls : kClasses) {
        const std::string c = cls;
        std::string key = c == "Alocação inicial" ? "allocation" : c;
        add("diag.pnl_usd." + key, "P&L US$ " + c, ' ', 0, 0,
            [c](const SeedResult& r){
                const auto it = r.by_class.find(c);
                return it == r.by_class.end() ? kNaN : it->second.gross * r.usd_per_tick_lot; });
        m.back().in_text = false;   // the class table shows it
        add("diag.net_usd." + key, "P&L líquido US$ " + c, ' ', 0, 0,
            [c](const SeedResult& r){
                const auto it = r.by_class.find(c);
                return it == r.by_class.end() ? kNaN
                     : (it->second.gross - it->second.fees / static_cast<double>(kFeeUnitsPerTick))
                       * r.usd_per_tick_lot; });
        m.back().in_text = false;
    }
    add("diag.fees_usd",     "taxas totais (receita da bolsa, US$)", ' ', 0, 0,
        [](const SeedResult& r){
            return static_cast<double>(r.zero_sum.fees) / static_cast<double>(kFeeUnitsPerTick)
                 * r.usd_per_tick_lot; });
    add("diag.zero_sum_ok",  "soma zero (posição e P&L bruto)", '>', 0.5, 0,
        [](const SeedResult& r){ return r.zero_sum.ok() ? 1.0 : 0.0; });
    add("diag.collapsed",    "seed colapsada (pregão com book de um lado)", '<', 0.5, 0,
        [](const SeedResult& r){ return r.collapsed() ? 1.0 : 0.0; });
    add("diag.max_day_gap",  "maior |log(mid/V)| de fechamento", '<', 0.01, 0,
        [](const SeedResult& r){
            double m = 0.0;
            for (double g : r.day_gap) if (std::isfinite(g)) m = std::max(m, std::abs(g));
            return m; });
    add("diag.min_day_two_sided", "pior pregão: fração com dois lados", '>', 0.99, 0,
        [](const SeedResult& r){
            return r.day_two_sided.empty() ? kNaN
                 : *std::min_element(r.day_two_sided.begin(), r.day_two_sided.end()); });
    add("diag.stops_fired",  "stops disparados",            ' ', 0, 0,
        [](const SeedResult& r){ return static_cast<double>(r.stops_fired); });
    add("diag.fills_mom",    "fills do momentum",           ' ', 0, 0,
        [](const SeedResult& r){ const auto it = r.by_class.find("MomentumTrader");
                                 return it == r.by_class.end() ? kNaN : double(it->second.fills); });
    add("diag.fills_mr",     "fills do mean-reverter",      ' ', 0, 0,
        [](const SeedResult& r){ const auto it = r.by_class.find("MeanReverterOU");
                                 return it == r.by_class.end() ? kNaN : double(it->second.fills); });
    add("diag.trade_sign_acf1", "ACF(sinal de trade, 1)",  'r', 0.10, 0.40,
        [](const SeedResult& r){ return r.report.trade_sign_acf1; });
    add("diag.vol_vol_corr", "corr vol-volume (1 s)",      ' ', 0, 0,
        [](const SeedResult& r){ return r.report.vol_vol_corr; });
    add("diag.hill",         "Hill α (1 s)",               ' ', 0, 0,
        [](const SeedResult& r){ return r.report.hill_alpha_mean; });
    add("diag.n_trades",     "trades por rodada",          ' ', 0, 0,
        [](const SeedResult& r){ return static_cast<double>(r.report.n_trades); });

    for (auto& x : m) {
        if (x.key.rfind("cal.", 0) == 0) x.set = Set::Cal;
        else if (x.key.rfind("val.", 0) == 0) x.set = Set::Val;
    }
    return m;
}

// The protocol lists keys; the table computes them.  A key in one and not
// the other means the frozen protocol and the tool have drifted apart.
bool protocol_matches(const std::vector<Metric>& table) {
    bool ok = true;
    const auto check = [&](const char* key, protocol::Set set) {
        const auto it = std::find_if(table.begin(), table.end(),
                                     [&](const Metric& m){ return m.key == key; });
        if (it == table.end() || it->set != set) {
            std::fprintf(stderr, "[ERROR] protocol key %s not computed as %s\n",
                         key, protocol::to_string(set));
            ok = false;
        }
    };
    for (const char* k : protocol::kCalMetrics) check(k, protocol::Set::Cal);
    for (const char* k : protocol::kValMetrics) check(k, protocol::Set::Val);
    for (const auto& m : table) {
        if (m.set == protocol::Set::Diag) continue;
        bool listed = false;
        for (const char* k : protocol::kCalMetrics) listed |= (m.key == k);
        for (const char* k : protocol::kValMetrics) listed |= (m.key == k);
        if (!listed) {
            std::fprintf(stderr, "[ERROR] metric %s is %s but not in Protocol.h\n",
                         m.key.c_str(), protocol::to_string(m.set));
            ok = false;
        }
    }
    return ok;
}

bool has_pass_rule(const Metric& m) { return m.cmp == '>' || m.cmp == '<' || m.cmp == 'r'; }

bool passes(const Metric& m, double x) {
    if (!std::isfinite(x)) return false;
    switch (m.cmp) {
        case '>': return x > m.lo;
        case '<': return x < m.lo;
        case 'r': return x >= m.lo && x <= m.hi;
        default:  return false;
    }
}

// Lower is better.  NaN for metrics with no preferred direction.
double loss(const Metric& m, double x) {
    if (!m.directional) return kNaN;
    switch (m.cmp) {
        case '>': case '+': return -x;
        case '<': case '-': return x;
        case 'r': return x < m.lo ? m.lo - x : (x > m.hi ? x - m.hi : 0.0);
        case 't': return std::abs(x - m.lo);
        default:  return kNaN;
    }
}

std::string target_text(const Metric& m) {
    char b[64] = "";
    switch (m.cmp) {
        case '>': std::snprintf(b, sizeof b, "> %g", m.lo); break;
        case '<': std::snprintf(b, sizeof b, "< %g", m.lo); break;
        case 'r': std::snprintf(b, sizeof b, "[%g; %g]", m.lo, m.hi); break;
        case 't': std::snprintf(b, sizeof b, "≈ %g", m.lo); break;
        case '+': std::snprintf(b, sizeof b, "maior"); break;
        case '-': std::snprintf(b, sizeof b, "menor"); break;
        default: break;
    }
    return b;
}

// Fixed width 10: four decimals for ordinary values, integers for P&L-sized
// ones, "n/d" when no seed could measure it.
std::string num(double x) {
    char b[32];
    if (!std::isfinite(x))        std::snprintf(b, sizeof b, "%10s", "n/d");
    else if (std::abs(x) >= 1e5)  std::snprintf(b, sizeof b, "%10.4g", x);
    else if (std::abs(x) >= 1e3)  std::snprintf(b, sizeof b, "%10.1f", x);
    else                          std::snprintf(b, sizeof b, "%10.4f", x);
    return b;
}

// printf pads by bytes; pt-BR labels carry multi-byte UTF-8 characters.
std::string pad(const std::string& s, size_t width) {
    size_t cols = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++cols;
    return cols >= width ? s : s + std::string(width - cols, ' ');
}

}  // namespace

// ── Ensemble text ────────────────────────────────────────────────────────────

struct RunInfo {
    std::string seed_set;     // "CAL", "VAL" or "livre"
    std::string overrides;    // "--set" list, empty if none
};

static std::string ensemble_text(const std::vector<SeedResult>& rs,
                                 const std::vector<uint64_t>& seeds,
                                 const SimulationConfig& cfg,
                                 const std::vector<Metric>& table,
                                 const RunInfo& info) {
    std::string out;
    if (rs.empty()) return out;
    const int n       = static_cast<int>(rs.size());
    const int n_facts = static_cast<int>(rs[0].report.results.size());

    append(out, "Relatório do ensemble (%d seeds × %llu ticks = %.1f pregões)\n", n,
           (unsigned long long)cfg.max_ticks, cfg.max_ticks / cfg.time.ticks_per_day());
    append(out, "=============================================================\n");
    append(out, "Seeds: %s (%llu..%llu)%s\n", info.seed_set.c_str(),
           (unsigned long long)seeds.front(), (unsigned long long)seeds.back(),
           info.seed_set == "livre" ? "  — fora do protocolo; ver Protocol.h" : "");
    if (info.seed_set == "VAL")
        out += "  VAL é hold-out: estes números julgam uma mudança e não orientam ajuste.\n";
    if (!info.overrides.empty()) append(out, "Overrides: %s\n", info.overrides.c_str());
    append(out, "Fundamental: %s   innovations: %s   sigma_annual = %.2f\n",
           rs[0].report.pd.has_fundamental ? "registrado" : "ausente",
           cfg.fundamental.gaussian_innovations ? "gaussianas" : "Student-t",
           cfg.fundamental.sigma_annual);
    append(out, "Notícias: %.1f/dia, impact scale %.4f\n", cfg.news.events_per_day,
           cfg.news.impact_scale);
    out += "Incerteza: SE = dp amostral entre seeds / √n.\n\n";

    // ── Legacy battery ──────────────────────────────────────────────────────
    out += "Bateria legada (1 s)\n";
    out += "| # | Teste                             |     Média |        SE |  Limiar | Passa |\n";
    out += "|---|-----------------------------------|-----------|-----------|---------|-------|\n";
    for (int f = 0; f < n_facts; ++f) {
        std::vector<double> vals;
        int pass_count = 0;
        for (const auto& r : rs) {
            if (f >= (int)r.report.results.size()) continue;
            vals.push_back(r.report.results[f].value);
            if (r.report.results[f].passed) ++pass_count;
        }
        const Stat st = summarize(vals);
        append(out, "| %d | %s | %9.4f | %9.4f | %7.4f | %2d/%-2d |\n",
               f + 1, pad(rs[0].report.results[f].name, 33).c_str(),
               st.mean, st.se, rs[0].report.results[f].threshold, pass_count, n);
    }

    // ── Metric table, grouped by protocol set ──────────────────────────────
    const auto section = [&](protocol::Set set, const char* title) {
        append(out, "\n%s\n", title);
        out += "| conj | métrica                          |      média |         SE |    mediana | alvo         | seeds ok |\n";
        out += "|------|----------------------------------|------------|------------|------------|--------------|----------|\n";
        for (const Metric& m : table) {
            if (m.set != set || !m.in_text) continue;
            std::vector<double> v;
            for (const auto& r : rs) v.push_back(m.get(r));
            const Stat st = summarize(v);
            std::string ok;
            if (has_pass_rule(m)) {
                const int k = (int)std::count_if(v.begin(), v.end(),
                                                 [&](double x){ return passes(m, x); });
                char b[32]; std::snprintf(b, sizeof b, "%d/%d", k, n); ok = b;
            }
            if (st.n < n) {
                char b[32]; std::snprintf(b, sizeof b, " (n=%d)", st.n); ok += b;
            }
            append(out, "| %-4s | %s | %s | %s | %s | %s | %8s |\n",
                   protocol::to_string(m.set), pad(m.label, 32).c_str(),
                   num(st.mean).c_str(), num(st.se).c_str(), num(st.median).c_str(),
                   pad(target_text(m), 12).c_str(), ok.c_str());
        }
    };
    section(protocol::Set::Cal,  "Calibração (CAL — pode orientar ajuste)");
    section(protocol::Set::Val,  "Validação (VAL — hold-out, nunca orienta ajuste)");

    // v2 verdicts per fact: the value alone does not say pass or fail.
    out += "\nBateria v2 por fato (seeds PASSA / FALHA / INCONCLUSIVO)\n";
    if (!rs[0].report.v2.facts.empty()) {
        for (const auto& f0 : rs[0].report.v2.facts) {
            int p = 0, fl = 0, inc = 0;
            for (const auto& r : rs) {
                const sfv2::FactResult* f = v2_fact(r, f0.id);
                if (!f) continue;
                if (f->status == sfv2::Status::Pass) ++p;
                else if (f->status == sfv2::Status::Fail) ++fl;
                else ++inc;
            }
            append(out, "  %-5s %s %2d / %2d / %2d\n", f0.id.c_str(),
                   pad(f0.name, 32).c_str(), p, fl, inc);
        }
    }

    section(protocol::Set::Diag, "Diagnósticos (não orientam ajuste nem julgam mudança)");

    // Which agent type is the worst offender, and how often it is that one.
    std::map<std::string, int> worst_counts;
    for (const auto& r : rs)
        if (!r.max_at_limit_type.empty()) ++worst_counts[r.max_at_limit_type];
    out += "  pior tipo no limite, por seed:";
    if (worst_counts.empty()) {
        out += " nenhum";
    } else {
        for (const auto& [name, k] : worst_counts)
            append(out, " %s x%d", name.c_str(), k);
    }
    out += "\n";

    // ── Who pays whom ───────────────────────────────────────────────────────
    out += "\nP&L MtM por classe (US$, média entre seeds ± SE; bruto = realizado + não realizado no último mid)\n";
    out += "| classe                 | agentes |   P&L bruto |          SE |      taxas | P&L líquido |      fills | posição fim | seeds > 0 |\n";
    out += "|------------------------|---------|-------------|-------------|------------|-------------|------------|-------------|-----------|\n";
    const double usd = rs[0].usd_per_tick_lot;
    double sum_gross = 0.0, sum_fees = 0.0;
    for (const char* cls : kClasses) {
        std::vector<double> g, fe, fl, nq;
        int agents = 0, pos = 0;
        for (const auto& r : rs) {
            const auto it = r.by_class.find(cls);
            if (it == r.by_class.end()) continue;
            g.push_back(it->second.gross * usd);
            fe.push_back(it->second.fees * usd / static_cast<double>(kFeeUnitsPerTick));
            fl.push_back(static_cast<double>(it->second.fills));
            nq.push_back(it->second.net);
            agents = it->second.agents;
            // Net of fees; with zero fees this is the gross count.
            if (it->second.gross * static_cast<double>(kFeeUnitsPerTick) > it->second.fees) ++pos;
        }
        if (g.empty()) continue;
        const Stat sg = summarize(g), sf = summarize(fe), sl = summarize(fl), sn = summarize(nq);
        sum_gross += sg.mean; sum_fees += sf.mean;
        char ok[16]; std::snprintf(ok, sizeof ok, "%d/%d", pos, (int)g.size());
        append(out, "| %s | %7d | %11.0f | %11.0f | %10.0f | %11.0f | %10.0f | %11.0f | %9s |\n",
               pad(cls, 22).c_str(), agents, sg.mean, sg.se, sf.mean, sg.mean - sf.mean,
               sl.mean, sn.mean, ok);
    }
    append(out, "| %s |         | %11.0f |             | %10.0f | %11.0f |            |             |           |\n",
           pad("Σ", 22).c_str(), sum_gross, sum_fees, sum_gross - sum_fees);
    const int zs_ok = (int)std::count_if(rs.begin(), rs.end(),
                                         [](const SeedResult& r){ return r.zero_sum.ok(); });
    append(out, "  Soma zero (Σ posição = 0 e Σ P&L bruto = 0, exatos em inteiros): %d/%d seeds.", zs_ok, n);
    if (zs_ok < n) {
        out += "  FALHA em:";
        for (size_t i = 0; i < rs.size(); ++i)
            if (!rs[i].zero_sum.ok())
                append(out, " s%llu (Σq=%lld, ΣP&L=%lld)", (unsigned long long)seeds[i],
                       (long long)rs[i].zero_sum.net_qty, (long long)rs[i].zero_sum.gross_pnl);
    }
    // Fees leave the system: Σ net = Σ gross − Σ fees = −Σ fees exactly, and the
    // class table must account for every fee the ledger booked.
    int fee_ok = 0; double fee_usd = 0.0;
    for (const auto& r : rs) {
        double by_cls = 0.0;
        for (const auto& [cls, c] : r.by_class) by_cls += c.fees;
        if (r.zero_sum.ok() && by_cls == static_cast<double>(r.zero_sum.fees)) ++fee_ok;
        fee_usd += static_cast<double>(r.zero_sum.fees) / static_cast<double>(kFeeUnitsPerTick)
                 * r.usd_per_tick_lot;
    }
    append(out, "\n  Σ P&L líquido = −Σ taxas (exato em unidades de taxa, classes cobrem o ledger): %d/%d seeds."
                "  Taxas totais: US$ %.0f por seed.\n", fee_ok, n, fee_usd / n);
    out += "  Taxas positivas = pagas; negativas = rebate recebido. Seeds > 0 conta o P&L líquido.\n";
    out += "  Posição fim = posição líquida da classe no último tick, em lotes (média entre seeds).\n";

    // ── Long horizon: does any seed stop being a market? ────────────────────
    if (!rs.empty() && rs[0].day_gap.size() > 1) {
        out += "\nHorizonte longo, por seed (fechamento de cada pregão)\n";
        out += "| seed | pregões | maior |gap| | gap final | pior pregão (2 lados) | colapso |\n";
        out += "|------|---------|-------------|-----------|-----------------------|---------|\n";
        for (size_t i = 0; i < rs.size(); ++i) {
            const auto& r = rs[i];
            double mx = 0.0, last = kNaN;
            for (double g : r.day_gap) if (std::isfinite(g)) { mx = std::max(mx, std::abs(g)); last = g; }
            const auto worst = std::min_element(r.day_two_sided.begin(), r.day_two_sided.end());
            char col[32];
            if (r.collapsed()) std::snprintf(col, sizeof col, "dia %d", r.collapse_day + 1);
            else               std::snprintf(col, sizeof col, "—");
            append(out, "| %4llu | %7zu | %10.3f%% | %8.3f%% | %13.2f%% (dia %3d) | %s |\n",
                   (unsigned long long)seeds[i], r.day_gap.size(), 100.0 * mx, 100.0 * last,
                   100.0 * *worst, (int)(worst - r.day_two_sided.begin()) + 1, pad(col, 7).c_str());
        }
        append(out, "  Colapso = pregão com menos de %.0f%% dos ticks com book de dois lados.\n",
               100.0 * kCollapseTwoSided);
    }

    // ── Per-seed fact counts and the headline pass rate ─────────────────────
    out += "\nFatos legados por seed:";
    for (size_t i = 0; i < rs.size(); ++i)
        append(out, " s%llu=%d/%d", (unsigned long long)seeds[i],
               rs[i].report.passed_count, rs[i].report.total_count);
    out += "\n";
    out += "Bateria v2 por seed (P/F/I):";
    for (size_t i = 0; i < rs.size(); ++i)
        append(out, " s%llu=%d/%d/%d", (unsigned long long)seeds[i],
               rs[i].report.v2.pass, rs[i].report.v2.fail, rs[i].report.v2.inconclusive);
    out += "\n";

    int total_pass = 0, total_facts = 0;
    for (const auto& r : rs) {
        total_pass  += r.report.passed_count;
        total_facts += r.report.total_count;
    }
    append(out, "\nLegado: %.1f%% dos fatos passaram no ensemble (%.1f/%.1f em média)\n",
           100.0 * total_pass / std::max(1, total_facts),
           static_cast<double>(total_pass) / n,
           static_cast<double>(total_facts) / n);

    append(out, "Ecologia: %d/%d seeds mantêm todo tipo de agente abaixo de 5%% dos ticks no limite.\n",
           (int)std::count_if(rs.begin(), rs.end(),
                              [](const SeedResult& r){ return r.max_at_limit_frac < 0.05; }), n);
    return out;
}

// ── Per-seed CSV and paired comparison ───────────────────────────────────────
// Long format: one line per (seed, metric), so adding a metric does not break
// an older baseline.  Lines starting with '#' carry the run description.

static bool write_csv(const std::string& path, const std::vector<SeedResult>& rs,
                      const std::vector<uint64_t>& seeds, const std::vector<Metric>& table,
                      const std::string& header) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# %s\n", header.c_str());
    std::fprintf(f, "seed,metric,set,value\n");
    for (size_t i = 0; i < rs.size(); ++i)
        for (const Metric& m : table)
            std::fprintf(f, "%llu,%s,%s,%.17g\n", (unsigned long long)seeds[i], m.key.c_str(),
                         protocol::to_string(m.set), m.get(rs[i]));
    std::fclose(f);
    return true;
}

struct Baseline {
    std::string header;
    std::map<std::string, std::map<uint64_t, double>> by_metric;   // metric → seed → value
};

static bool read_csv(const std::string& path, Baseline& b) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty()) continue;
        if (s[0] == '#') { if (b.header.empty()) b.header = s.substr(s.find_first_not_of("# ")); continue; }
        if (s.rfind("seed,", 0) == 0) continue;
        const auto c1 = s.find(','), c2 = s.find(',', c1 + 1), c3 = s.find(',', c2 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos) continue;
        const uint64_t seed = std::stoull(s.substr(0, c1));
        const std::string key = s.substr(c1 + 1, c2 - c1 - 1);
        const std::string val = s.substr(c3 + 1);
        double v = kNaN;
        try { v = std::stod(val); } catch (...) {}   // "nan" parses; anything else stays NaN
        b.by_metric[key][seed] = v;
    }
    std::fclose(f);
    return true;
}

static std::string paired_text(const Baseline& b, const std::string& base_path,
                               const std::vector<SeedResult>& rs,
                               const std::vector<uint64_t>& seeds,
                               const std::vector<Metric>& table,
                               const std::string& header) {
    std::string out;
    append(out, "\nComparação pareada contra %s\n", base_path.c_str());
    out += "------------------------------------------------------------\n";
    append(out, "  base:  %s\n  atual: %s\n", b.header.c_str(), header.c_str());
    append(out, "  Δ = atual − base, seed a seed; SE(Δ) = dp(Δ)/√n.  Melhora ou piora só com\n"
                "  |Δ| > %.0f·SE, medido sobre a perda (distância ao alvo) quando a métrica tem alvo.\n\n",
           protocol::kSignificanceSE);
    out += "| conj | métrica                          | n  |       base |      atual |          Δ |      SE(Δ) | veredito         |\n";
    out += "|------|----------------------------------|----|------------|------------|------------|------------|------------------|\n";
    int better = 0, worse = 0;
    for (const Metric& m : table) {
        const auto it = b.by_metric.find(m.key);
        if (it == b.by_metric.end()) continue;
        std::vector<double> base_v, cur_v, d_val, d_loss;
        for (size_t i = 0; i < rs.size(); ++i) {
            const auto js = it->second.find(seeds[i]);
            if (js == it->second.end()) continue;
            const double x0 = js->second, x1 = m.get(rs[i]);
            if (!std::isfinite(x0) || !std::isfinite(x1)) continue;
            base_v.push_back(x0); cur_v.push_back(x1);
            d_val.push_back(x1 - x0);
            d_loss.push_back(loss(m, x1) - loss(m, x0));
        }
        const int np = static_cast<int>(d_val.size());
        if (np == 0) continue;
        const Stat sb = summarize(base_v), sc = summarize(cur_v), sd = summarize(d_val);
        const bool directional = std::isfinite(loss(m, 0.0));
        const Stat sl = directional ? summarize(d_loss) : sd;
        std::string verdict;
        const bool all_zero = std::all_of(d_val.begin(), d_val.end(),
                                            [](double x){ return x == 0.0; });
        if (all_zero)        verdict = "idêntico";
        else if (np < 2)     verdict = "n insuficiente";
        else {
            const bool sig = (sl.se == 0.0) ? (sl.mean != 0.0)
                                            : std::abs(sl.mean) > protocol::kSignificanceSE * sl.se;
            if (!sig)              verdict = "dentro do ruído";
            else if (!directional) verdict = sd.mean > 0 ? "sobe" : "desce";
            else if (sl.mean < 0) { verdict = "MELHORA"; if (m.set != protocol::Set::Diag) ++better; }
            else                  { verdict = "PIORA";   if (m.set != protocol::Set::Diag) ++worse; }
        }
        append(out, "| %-4s | %s | %2d | %s | %s | %s | %s | %s |\n",
               protocol::to_string(m.set), pad(m.label, 32).c_str(), np,
               num(sb.mean).c_str(), num(sc.mean).c_str(), num(sd.mean).c_str(),
               num(sd.se).c_str(), pad(verdict, 16).c_str());
        // For a range or a point target the verdict is on the distance to it,
        // whose paired SE is not the SE of the value; show the one it used.
        if (directional && (m.cmp == 'r' || m.cmp == 't'))
            append(out, "|      |   └ distância ao alvo            |    |            |            | %s | %s |                  |\n",
                   num(sl.mean).c_str(), num(sl.se).c_str());
    }
    append(out, "\n  CAL+VAL: %d melhora(s), %d piora(s) significativas.\n", better, worse);
    return out;
}

// ── main ─────────────────────────────────────────────────────────────────────

// --set key=value: population overrides for ablations, without a rebuild.
// Every override is echoed in the report header, so a result cannot be read
// without knowing which world produced it.
static bool apply_set(SimulationConfig& c, const std::string& kv) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) return false;
    const std::string k = kv.substr(0, eq);
    const double      v = std::stod(kv.substr(eq + 1));
    auto& P = c.population;
    const auto I = [&](int& f) { f = static_cast<int>(std::llround(v)); return true; };
    if (k == "price_ref")        { P.price_ref_ticks = v; return true; }
    if (k == "mm.count")         return I(P.market_makers.count);
    if (k == "noise.count")      return I(P.noise_traders.count);
    if (k == "inf.count")        return I(P.informed_traders.count);
    if (k == "inf.max_position") return I(P.informed_traders.max_position);
    if (k == "inf.q_soft_frac")  { P.informed_traders.q_soft_frac = v; return true; }
    if (k == "mom.count")        return I(P.momentum_traders.count);
    if (k == "mr.count")         return I(P.mean_reverters.count);
    if (k == "mom.qty")          return I(P.momentum_traders.qty_override);
    if (k == "mr.qty")           return I(P.mean_reverters.qty_override);
    // <= 0: the maker's belief moves linearly in OFI (Kyle); > 0: square root.
    if (k == "mm.impact_q_ref")  { P.market_makers.impact_q_ref = v; return true; }
    if (k == "mm.lambda_kyle")   { P.market_makers.lambda_kyle = v; return true; }
    if (k == "mm.adverse_sel")   { P.market_makers.adverse_sel_ticks_per_lot = v; return true; }
    if (k == "mm.news_noise")    { P.market_makers.news_signal_noise = v; return true; }
    if (k == "mm.news_blind")    { P.market_makers.news_blind = v != 0.0; return true; }
    if (k == "mm.random_cadence") { P.market_makers.random_cadence = v != 0.0; return true; }
    if (k == "mm.accumulate_ofi") { P.market_makers.accumulate_ofi = v != 0.0; return true; }
    if (k == "vi.count")         return I(P.value_investors.count);
    if (k == "lot.count")        return I(P.limit_traders.count);
    if (k == "lot.k_max")        { P.limit_traders.k_max = v; return true; }
    if (k == "lot.p_act_scale")  { P.limit_traders.p_act_range.min_val *= v;
                                   P.limit_traders.p_act_range.max_val *= v; return true; }
    if (k == "inst.count")       return I(P.institutionals.count);
    if (k == "sl.count")         return I(P.stop_loss.count);
    if (k == "nr.count")         return I(P.news_reactors.count);
    if (k == "nr.base_qty")      return I(P.news_reactors.base_qty);
    if (k == "nr.max_position")  return I(P.news_reactors.max_position);
    if (k == "nr.hold_min")       { P.news_reactors.hold_range.min_val = v; return true; }
    if (k == "nr.hold_max")       { P.news_reactors.hold_range.max_val = v; return true; }
    return false;
}

int main(int argc, char** argv) {
    std::vector<std::string> overrides;
    int         n_seeds    = 10;
    uint64_t    seed_start = 0;
    int         jobs       = 1;
    double      days       = 0.0;    // > 0 overrides --duration
    bool        keep_logs  = false;
    std::string outdir  = "logs/calibrate/";
    std::string save_path;
    std::string csv_path;
    std::string baseline_path;
    std::string seed_set = "livre";
    bool        seeds_given = false;
    SimulationConfig base_cfg;
    base_cfg.max_ticks = 50'000;

    // The loop runs to argc so that a valueless flag in last position is still
    // seen; flags that take a value check for it before reading.
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const bool has_val = (i + 1 < argc);
        if (key == "--gaussian") { base_cfg.fundamental.gaussian_innovations = true; continue; }
        if (key == "--news-blind") { base_cfg.population.market_makers.news_blind = true;
                                     overrides.push_back("mm.news_blind=1"); continue; }
        if (key == "--news-noise") {
            if (i + 1 >= argc) { std::fprintf(stderr, "[ERROR] --news-noise needs a value\n"); return 2; }
            base_cfg.population.market_makers.news_signal_noise = std::stod(argv[i+1]);
            overrides.push_back(std::string("mm.news_noise=") + argv[i+1]);
            ++i; continue;
        }
        if (key == "--keep-logs") { keep_logs = true; continue; }
        if (key == "--taker-fee" || key == "--maker-rebate") {
            if (i + 1 >= argc) { std::fprintf(stderr, "[ERROR] %s needs a value (bp)\n", key.c_str()); return 2; }
            const double bp = std::stod(argv[i+1]);
            if (key == "--taker-fee") base_cfg.taker_fee_bp = bp; else base_cfg.maker_rebate_bp = bp;
            overrides.push_back((key == "--taker-fee" ? std::string("fee.taker_bp=") : std::string("fee.rebate_bp=")) + argv[i+1]);
            ++i; continue;
        }
        // Every other flag takes a value.  An unknown argument is an error:
        // ignoring it ran whole ensembles on the defaults when a shell passed
        // several flags as one word.
        static const char* const kValued[] = {
            "--set", "--seeds", "--seed-set", "--csv", "--baseline", "--duration", "--days",
            "--seed-start", "--jobs", "--outdir", "--save", "--ticker", "--sigma-annual",
            "--process" };
        if (std::find_if(std::begin(kValued), std::end(kValued),
                         [&](const char* f) { return key == f; }) == std::end(kValued)) {
            std::fprintf(stderr, "[ERROR] unknown argument '%s'\n", key.c_str());
            return 2;
        }
        if (!has_val) {
            std::fprintf(stderr, "[ERROR] %s needs a value\n", key.c_str());
            return 2;
        }
        if (key == "--set") {
            if (!apply_set(base_cfg, argv[i+1])) {
                std::fprintf(stderr, "[ERROR] unknown --set %s\n", argv[i+1]);
                return 2;
            }
            overrides.push_back(argv[i+1]);
        }
        if (key == "--seeds")    { n_seeds = std::stoi(argv[i+1]); seeds_given = true; }
        if (key == "--seed-set") {
            const std::string v = argv[i+1];
            if      (v == "cal") seed_set = "CAL";
            else if (v == "val") seed_set = "VAL";
            else { std::fprintf(stderr, "[ERROR] --seed-set takes cal or val\n"); return 2; }
        }
        if (key == "--csv")      csv_path           = argv[i+1];
        if (key == "--baseline") baseline_path      = argv[i+1];
        if (key == "--duration") base_cfg.max_ticks = std::stoull(argv[i+1]);
        if (key == "--days")       days       = std::stod(argv[i+1]);
        if (key == "--seed-start") { seed_start = std::stoull(argv[i+1]); seeds_given = true; }
        if (key == "--jobs")       jobs       = std::max(1, std::stoi(argv[i+1]));
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
        ++i;   // the value was consumed
    }

    const std::vector<Metric> table = metric_table();
    if (!protocol_matches(table)) return 3;

    if (seed_set != "livre") {
        if (seeds_given) {
            std::fprintf(stderr, "[ERROR] --seed-set fixes the seeds; drop --seeds / --seed-start\n");
            return 2;
        }
        const protocol::SeedRange sr = seed_set == "CAL" ? protocol::kCalSeeds : protocol::kValSeeds;
        seed_start = sr.first;
        n_seeds    = sr.count;
    }
    if (days > 0.0)
        base_cfg.max_ticks = static_cast<uint64_t>(std::llround(days * base_cfg.time.ticks_per_day()));
    if (n_seeds < 1) n_seeds = 1;
    jobs = std::min(jobs, n_seeds);
    if (keep_logs) std::filesystem::create_directories(outdir);
    if (csv_path.empty()) csv_path = (std::filesystem::path(outdir) / "per_seed.csv").string();

    // Load the baseline before the run, so a bad path fails in a second and
    // not after an hour of simulation.
    Baseline baseline;
    if (!baseline_path.empty() && !read_csv(baseline_path, baseline)) {
        std::fprintf(stderr, "[ERROR] could not read baseline %s\n", baseline_path.c_str());
        return 1;
    }

    std::fprintf(stderr, "[INFO] sim_calibrate: seeds=%llu..%llu  ticks=%llu (%.1f days)  jobs=%d  logs=%s  innovations=%s\n",
                 (unsigned long long)seed_start,
                 (unsigned long long)(seed_start + n_seeds - 1),
                 (unsigned long long)base_cfg.max_ticks,
                 base_cfg.max_ticks / base_cfg.time.ticks_per_day(), jobs,
                 keep_logs ? outdir.c_str() : "memory only",
                 base_cfg.fundamental.gaussian_innovations ? "gaussian" : "student-t");

    // Results are stored by seed index, so the report does not depend on which
    // thread finishes first.
    std::vector<SeedResult> results(static_cast<size_t>(n_seeds));
    std::atomic<int> next{0};
    std::mutex       io_mu;
    int              done = 0;

    auto worker = [&]() {
        for (int i = next.fetch_add(1); i < n_seeds; i = next.fetch_add(1)) {
            const uint64_t seed = seed_start + static_cast<uint64_t>(i);
            const std::string log_path = keep_logs
                ? outdir + "seed_" + std::to_string(seed) + ".bin" : std::string{};
            results[static_cast<size_t>(i)] = run_one(base_cfg, seed, log_path);
            const SeedResult& r = results[static_cast<size_t>(i)];
            std::lock_guard<std::mutex> lk(io_mu);
            ++done;
            std::fprintf(stderr, "  [%d/%d] seed=%llu  %d/%d facts   corr_h100=%.3f  gap_std=%.4f  worst-at-limit=%.2f%% (%s)\n",
                         done, n_seeds, (unsigned long long)seed,
                         r.report.passed_count, r.report.total_count,
                         r.report.pd.corr_h100, r.report.pd.gap_std,
                         100.0 * r.max_at_limit_frac, r.max_at_limit_type.c_str());
        }
    };
    // One seed needs more stack than the 512 KB macOS gives a secondary
    // std::thread (it dies with SIGBUS on the guard page); the main thread has
    // 8 MB.  Spawn the workers as pthreads with an explicit stack instead.
    std::function<void()> work = worker;
    auto trampoline = [](void* p) -> void* {
        (*static_cast<std::function<void()>*>(p))();
        return nullptr;
    };
    std::vector<pthread_t> pool;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u << 20);
    for (int j = 1; j < jobs; ++j) {
        pthread_t th;
        if (pthread_create(&th, &attr, trampoline, &work) == 0) pool.push_back(th);
        else std::fprintf(stderr, "[WARN] could not start worker %d; continuing with fewer\n", j);
    }
    pthread_attr_destroy(&attr);
    worker();
    for (pthread_t th : pool) pthread_join(th, nullptr);

    std::vector<uint64_t> seeds(static_cast<size_t>(n_seeds));
    for (int i = 0; i < n_seeds; ++i) seeds[static_cast<size_t>(i)] = seed_start + static_cast<uint64_t>(i);

    RunInfo info;
    info.seed_set = seed_set;
    for (const auto& kv : overrides) info.overrides += (info.overrides.empty() ? "" : " ") + kv;

    // One line that says which world produced the numbers; it goes into the
    // CSV and is echoed when that CSV is later used as a baseline.
    char hdr[512];
    std::snprintf(hdr, sizeof hdr, "seeds=%s %llu..%llu ticks=%llu innovations=%s sigma_annual=%.2f overrides=%s",
                  seed_set.c_str(), (unsigned long long)seeds.front(), (unsigned long long)seeds.back(),
                  (unsigned long long)base_cfg.max_ticks,
                  base_cfg.fundamental.gaussian_innovations ? "gaussian" : "student-t",
                  base_cfg.fundamental.sigma_annual,
                  info.overrides.empty() ? "none" : info.overrides.c_str());
    const std::string header = hdr;

    // A collapsed seed is not a market: its facts are reported apart and stay
    // out of the ensemble means.  The CSV and the paired comparison keep every
    // seed (pairing needs the same seeds on both sides; diag.collapsed flags them).
    std::vector<SeedResult> ok_rs, bad_rs;
    std::vector<uint64_t>   ok_seeds, bad_seeds;
    for (size_t i = 0; i < results.size(); ++i) {
        auto& rs_ = results[i].collapsed() ? bad_rs : ok_rs;
        auto& ss_ = results[i].collapsed() ? bad_seeds : ok_seeds;
        rs_.push_back(results[i]); ss_.push_back(seeds[i]);
    }
    std::string text;
    if (bad_rs.empty()) {
        text = ensemble_text(results, seeds, base_cfg, table, info);
    } else {
        text = "ATENÇÃO: " + std::to_string(bad_rs.size()) + " seed(s) colapsada(s); as médias abaixo usam só as "
             + std::to_string(ok_rs.size()) + " restantes.\n";
        if (!ok_rs.empty()) text += ensemble_text(ok_rs, ok_seeds, base_cfg, table, info);
        text += "\n\n=== Seeds colapsadas (à parte, não são mercado) ===\n";
        text += ensemble_text(bad_rs, bad_seeds, base_cfg, table, info);
    }
    if (!baseline_path.empty())
        text += paired_text(baseline, baseline_path, results, seeds, table, header);
    std::printf("\n%s", text.c_str());

    {
        const std::string daily = (std::filesystem::path(csv_path).parent_path() / "daily.csv").string();
        if (FILE* f = std::fopen(daily.c_str(), "w")) {
            std::fprintf(f, "# %s\nseed,day,gap_log,two_sided\n", header.c_str());
            for (size_t i = 0; i < results.size(); ++i)
                for (size_t day = 0; day < results[i].day_gap.size(); ++day)
                    std::fprintf(f, "%llu,%zu,%.10g,%.6f\n", (unsigned long long)seeds[i], day + 1,
                                 results[i].day_gap[day], results[i].day_two_sided[day]);
            std::fclose(f);
        }
    }
    if (write_csv(csv_path, results, seeds, table, header))
        std::fprintf(stderr, "[INFO] per-seed metrics written to %s\n", csv_path.c_str());
    else
        std::fprintf(stderr, "[WARN] could not write %s\n", csv_path.c_str());

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
