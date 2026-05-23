#include "analysis/Report.h"
#include "analysis/AcfComputer.h"
#include "analysis/HillEstimator.h"
#include "analysis/FlashCrashDetector.h"
#include "persistence/BinaryLogReader.h"
#include "persistence/EventSchema.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>

// ── helpers ───────────────────────────────────────────────────────────────────

static double mean_of(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

static double variance_of(const std::vector<double>& v, double m) {
    if (v.size() < 2) return 0.0;
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return s / v.size();
}

static double kurtosis_excess(const std::vector<double>& v) {
    int n = static_cast<int>(v.size());
    if (n < 4) return 0.0;
    double m = mean_of(v);
    double s2 = variance_of(v, m);
    if (s2 < 1e-30) return 0.0;
    double m4 = 0.0;
    for (double x : v) { double d = x - m; m4 += d * d * d * d; }
    return (m4 / n) / (s2 * s2) - 3.0;
}

static double skewness(const std::vector<double>& v) {
    int n = static_cast<int>(v.size());
    if (n < 3) return 0.0;
    double m  = mean_of(v);
    double s2 = variance_of(v, m);
    if (s2 < 1e-30) return 0.0;
    double m3 = 0.0;
    for (double x : v) { double d = x - m; m3 += d * d * d; }
    return (m3 / n) / std::pow(s2, 1.5);
}

static StyleResult make(const std::string& name,
                        bool passed,
                        double value,
                        double threshold,
                        const std::string& desc) {
    return {name, passed, value, threshold, desc};
}

// ── data extraction ───────────────────────────────────────────────────────────

struct LogData {
    std::vector<double>   mid_prices;
    std::vector<uint64_t> price_ticks;
    std::vector<double>   spreads;
    std::vector<double>   ofi;
    size_t                n_trades    = 0;
    size_t                n_snapshots = 0;
};

static LogData extract(const std::string& bin_path) {
    LogData d;
    BinaryLogReader r(bin_path);
    if (!r.read_header()) return d;

    while (auto rec = r.next()) {
        if (auto* sr = std::get_if<MarketSnapshotRecord>(&*rec)) {
            if (sr->mid_price > 0) {
                d.mid_prices.push_back(static_cast<double>(sr->mid_price));
                d.price_ticks.push_back(sr->tick);
                d.spreads.push_back(static_cast<double>(sr->spread));
                d.ofi.push_back(sr->ofi_tick);
            }
            ++d.n_snapshots;
        } else if (std::get_if<TradeRecord>(&*rec)) {
            ++d.n_trades;
        }
    }
    return d;
}

static std::vector<double> log_returns(const std::vector<double>& prices) {
    std::vector<double> rets;
    rets.reserve(prices.size());
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i - 1] > 0.0 && prices[i] > 0.0)
            rets.push_back(std::log(prices[i] / prices[i - 1]));
    }
    return rets;
}

// ── Report::run ───────────────────────────────────────────────────────────────

AnalysisReport Report::run(const std::string& bin_path) {
    AnalysisReport rep;

    LogData d = extract(bin_path);
    rep.n_trades    = d.n_trades;
    rep.n_snapshots = d.n_snapshots;

    if (d.mid_prices.size() < 100) {
        // Not enough data — mark all as failed.
        for (int i = 1; i <= 8; ++i)
            rep.results.push_back({"Fact " + std::to_string(i), false, 0, 0, "insufficient data"});
        rep.total_count = 8;
        return rep;
    }

    std::vector<double> rets = log_returns(d.mid_prices);
    rep.n_returns = rets.size();

    // ── Fact 1: Fat tails (excess kurtosis > 1.0) ─────────────────────────────
    {
        double kurt = kurtosis_excess(rets);
        rep.results.push_back(make(
            "Fat tails (kurtosis)",
            kurt > 1.0,
            kurt, 1.0,
            "excess kurtosis of log-returns > 1.0"));
    }

    // ── ACF of returns and |returns| ──────────────────────────────────────────
    constexpr int kMaxLag = 20;
    auto acf_ret = AcfComputer::compute(rets, kMaxLag);
    auto acf_abs = AcfComputer::compute_abs(rets, kMaxLag);

    // ── Fact 2: Return ACF ≈ 0 at lag 1 ──────────────────────────────────────
    {
        double val  = acf_ret.acf.size() > 1 ? std::abs(acf_ret.acf[1]) : 1.0;
        // Fixed threshold: economically "approximately zero" for a simulation.
        // 2/sqrt(N) is too tight at large N (statistically rejects any real market).
        constexpr double kAcfThreshold = 0.10;
        rep.results.push_back(make(
            "Return ACF ≈ 0 (lag 1)",
            val < kAcfThreshold,
            val, kAcfThreshold,
            "|ACF(r, lag=1)| < 0.10 — returns approximately serially uncorrelated"));
    }

    // ── Fact 3: Volatility clustering (ACF|r| lag=1 > 0.05) ──────────────────
    {
        double val = acf_abs.acf.size() > 1 ? acf_abs.acf[1] : 0.0;
        rep.results.push_back(make(
            "Vol clustering (ACF|r| lag=1)",
            val > 0.05,
            val, 0.05,
            "ACF(|r|, lag=1) > 0.05 — volatility is serially correlated"));
    }

    // ── Fact 4: Long memory of volatility (mean ACF|r| lags 1..10 > 0.02) ────
    {
        double sum = 0.0;
        int    cnt = 0;
        for (int lag = 1; lag <= std::min(10, kMaxLag) && lag < (int)acf_abs.acf.size(); ++lag) {
            sum += acf_abs.acf[lag];
            ++cnt;
        }
        double val = (cnt > 0) ? sum / cnt : 0.0;
        rep.results.push_back(make(
            "Long memory of vol (mean ACF|r|)",
            val > 0.02,
            val, 0.02,
            "mean ACF(|r|, lag=1..10) > 0.02 — slow decay of volatility memory"));
    }

    // ── Fact 5: Gain-loss asymmetry (skewness < 0) ────────────────────────────
    {
        double skew = skewness(rets);
        rep.results.push_back(make(
            "Gain-loss asymmetry (skew < 0)",
            skew < 0.0,
            skew, 0.0,
            "skewness(r) < 0 — losses larger than gains (crash regime effect)"));
    }

    // ── Fact 6: Hill tail index α̂ ∈ [1.8, 6.0] ───────────────────────────────
    {
        auto hill = HillEstimator::estimate_abs(rets);
        double val = hill.valid ? hill.alpha : 0.0;
        bool   ok  = hill.valid && val >= 1.8 && val <= 6.0;
        rep.results.push_back(make(
            "Hill tail index α ∈ [1.8, 6.0]",
            ok,
            val, 1.8,   // lower bound shown as threshold
            "power-law tail index of |returns| in typical financial range"));
    }

    // ── Fact 7: Spread variation (CoV(spread) > 0.08) ─────────────────────────
    // Only include ticks with an active two-sided book (spread > 0).
    // Zero-spread periods (one-sided book) carry no information about spread variation.
    {
        double val = 0.0;
        std::vector<double> pos_spreads;
        for (double sp : d.spreads)
            if (sp > 0.0) pos_spreads.push_back(sp);
        if (!pos_spreads.empty()) {
            double m = mean_of(pos_spreads);
            double s = std::sqrt(variance_of(pos_spreads, m));
            val = (m > 0.0) ? s / m : 0.0;
        }
        rep.results.push_back(make(
            "Spread variation (CoV > 0.08)",
            val > 0.08,
            val, 0.08,
            "coefficient of variation of bid-ask spread > 0.08 (two-sided book only)"));
    }

    // ── Fact 8: OFI clustering (ACF(OFI, lag=1) > 0.03) ─────────────────────
    {
        double val = 0.0;
        if (d.ofi.size() > static_cast<size_t>(kMaxLag + 2)) {
            auto acf_ofi = AcfComputer::compute(d.ofi, kMaxLag);
            if (acf_ofi.acf.size() > 1)
                val = std::abs(acf_ofi.acf[1]);
        }
        rep.results.push_back(make(
            "OFI clustering (ACF|OFI| lag=1 > 0.03)",
            val > 0.03,
            val, 0.03,
            "order-flow imbalance shows serial correlation — flow toxicity clusters"));
    }

    // ── Flash crash detection (informational only) ────────────────────────────
    {
        FlashCrashDetector::Config fcc{4.0, 10, 30, 0.50, 30}; // relaxed threshold
        auto evts = FlashCrashDetector::detect(d.mid_prices, d.price_ticks, fcc);
        rep.flash_crashes = static_cast<int>(evts.size());
    }

    rep.total_count = static_cast<int>(rep.results.size());
    for (const auto& r2 : rep.results) rep.passed_count += r2.passed ? 1 : 0;
    return rep;
}

// ── Formatting ────────────────────────────────────────────────────────────────

std::string AnalysisReport::to_text() const {
    std::ostringstream os;
    os << "Stylized-Facts Report\n";
    os << "=====================\n";
    os << "Samples: " << n_returns << " returns  "
       << n_trades << " trades  "
       << n_snapshots << " snapshots\n";
    os << "Flash crashes detected: " << flash_crashes << "\n\n";
    os << "| # | Test                              | Value     | Threshold | Pass |\n";
    os << "|---|-----------------------------------|-----------|-----------|------|\n";
    int idx = 1;
    for (const auto& r : results) {
        char row[256];
        std::snprintf(row, sizeof(row),
            "| %d | %-33s | %9.4f | %9.4f | %s  |\n",
            idx++,
            r.name.c_str(),
            r.value, r.threshold,
            r.passed ? "PASS" : "FAIL");
        os << row;
    }
    os << "\nResult: " << passed_count << "/" << total_count << " facts validated\n";
    return os.str();
}

void AnalysisReport::to_csv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "rank,name,value,threshold,passed,description\n");
    int idx = 1;
    for (const auto& r : results) {
        std::fprintf(f, "%d,\"%s\",%.6f,%.6f,%s,\"%s\"\n",
            idx++, r.name.c_str(), r.value, r.threshold,
            r.passed ? "true" : "false", r.description.c_str());
    }
    std::fclose(f);
}
