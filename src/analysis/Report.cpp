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

static double pearson_corr(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = std::min(x.size(), y.size());
    if (n < 4) return 0.0;
    double mx = 0.0, my = 0.0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double num = 0.0, dx2 = 0.0, dy2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        num += dx * dy; dx2 += dx * dx; dy2 += dy * dy;
    }
    double denom = std::sqrt(dx2 * dy2);
    return (denom > 1e-30) ? num / denom : 0.0;
}

// Ljung-Box Q statistic: Q = N(N+2) * sum_{k=1}^{m} ACF(k)^2 / (N-k)
// Under H0 (white noise) Q ~ chi^2(m). Large Q → reject H0 (returns autocorrelated).
static double ljung_box_q(const std::vector<double>& acf, int n, int m) {
    double q = 0.0;
    for (int k = 1; k <= m && k < (int)acf.size(); ++k)
        q += (acf[k] * acf[k]) / (n - k);
    return static_cast<double>(n) * (n + 2) * q;
}

// Upper-tail p-value of a chi^2(df) variate, Wilson-Hilferty cube-root
// approximation (accurate to ~1e-3 for df >= 5, which is all we need here).
static double chi2_sf(double q, int df) {
    if (df <= 0) return 1.0;
    if (q <= 0.0) return 1.0;
    const double d = static_cast<double>(df);
    double z = (std::cbrt(q / d) - (1.0 - 2.0 / (9.0 * d))) / std::sqrt(2.0 / (9.0 * d));
    return 0.5 * std::erfc(z / std::sqrt(2.0));
}

// corr(log(a_{t+h}/a_t), log(b_{t+h}/b_t)) over NON-overlapping blocks of h.
// Non-overlapping avoids the spurious inflation that overlapping windows give.
static double horizon_corr(const std::vector<double>& a,
                           const std::vector<double>& b, size_t h) {
    size_t n = std::min(a.size(), b.size());
    if (h == 0 || n < 4 * h) return 0.0;
    std::vector<double> ra, rb;
    for (size_t i = h; i < n; i += h) {
        if (a[i] <= 0.0 || a[i - h] <= 0.0 || b[i] <= 0.0 || b[i - h] <= 0.0) continue;
        ra.push_back(std::log(a[i] / a[i - h]));
        rb.push_back(std::log(b[i] / b[i - h]));
    }
    return pearson_corr(ra, rb);
}

// Lag-1 autocorrelation, computed directly (AcfComputer would do, but this
// keeps the gap series independent of the max-lag machinery).
static double ar1_rho(const std::vector<double>& v) {
    if (v.size() < 8) return 0.0;
    double m = mean_of(v);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        double d = v[i] - m;
        den += d * d;
        if (i + 1 < v.size()) num += d * (v[i + 1] - m);
    }
    return (den > 1e-30) ? num / den : 0.0;
}

// Multi-k Hill: run Hill at several k values and return mean/std of alpha estimates.
static void hill_multi_k(const std::vector<double>& rets,
                         double& mean_alpha, double& std_alpha) {
    int n = static_cast<int>(rets.size());
    if (n < 20) { mean_alpha = std_alpha = 0.0; return; }
    // k = n^{0.35}, n^{0.45}, n^{0.5}, n^{0.55}, n^{0.65}
    static const double exps[] = {0.35, 0.45, 0.50, 0.55, 0.65};
    std::vector<double> alphas;
    for (double e : exps) {
        int k = static_cast<int>(std::ceil(std::pow(n, e)));
        auto h = HillEstimator::estimate_abs(rets, k);
        if (h.valid && h.alpha > 0.0 && h.alpha < 50.0)
            alphas.push_back(h.alpha);
    }
    if (alphas.empty()) { mean_alpha = std_alpha = 0.0; return; }
    mean_alpha = std::accumulate(alphas.begin(), alphas.end(), 0.0) / alphas.size();
    double var = 0.0;
    for (double a : alphas) var += (a - mean_alpha) * (a - mean_alpha);
    std_alpha = (alphas.size() > 1) ? std::sqrt(var / alphas.size()) : 0.0;
}

static StyleResult make(const std::string& name,
                        bool passed,
                        double value,
                        double threshold,
                        const std::string& desc,
                        double ci_band = 0.0) {
    return {name, passed, value, threshold, ci_band, desc};
}

// ── data extraction ───────────────────────────────────────────────────────────

struct LogData {
    std::vector<double>   mid_prices;
    std::vector<uint64_t> price_ticks;
    std::vector<double>   spreads;
    std::vector<double>   ofi;
    std::vector<double>   fundamental;       // latent V in ticks (empty on v2 logs)
    std::vector<double>   trade_signs;       // +1 = buy-aggressor, -1 = sell-aggressor
    std::vector<double>   volume_per_snap;   // total trade volume since last snapshot
    size_t                n_trades    = 0;
    size_t                n_snapshots = 0;
    size_t                n_two_sided = 0;   // snapshots with spread > 0
    double                mm_volume   = 0.0; // volume with MM maker and MM taker
    double                total_volume = 0.0;
    bool                  has_fundamental = false;
};

static LogData extract(const std::string& bin_path,
                       const std::vector<AgentId>& mm_ids) {
    LogData d;
    BinaryLogReader r(bin_path);
    if (!r.read_header()) return d;

    auto is_mm = [&](AgentId a) {
        return std::find(mm_ids.begin(), mm_ids.end(), a) != mm_ids.end();
    };

    double pending_volume = 0.0;

    while (auto rec = r.next()) {
        if (auto* sr = std::get_if<MarketSnapshotRecord>(&*rec)) {
            if (sr->mid_price > 0) {
                d.mid_prices.push_back(static_cast<double>(sr->mid_price));
                d.price_ticks.push_back(sr->tick);
                d.spreads.push_back(static_cast<double>(sr->spread));
                d.ofi.push_back(sr->ofi_tick);
                d.fundamental.push_back(sr->fundamental_value);
                d.volume_per_snap.push_back(pending_volume);
                pending_volume = 0.0;
                if (sr->spread > 0) ++d.n_two_sided;
                if (sr->fundamental_value > 0.0) d.has_fundamental = true;
            }
            ++d.n_snapshots;
        } else if (auto* tr = std::get_if<TradeRecord>(&*rec)) {
            d.trade_signs.push_back(tr->taker_side == 0 ? 1.0 : -1.0);
            pending_volume += static_cast<double>(tr->qty);
            d.total_volume += static_cast<double>(tr->qty);
            if (!mm_ids.empty() && is_mm(tr->maker_agent) && is_mm(tr->taker_agent))
                d.mm_volume += static_cast<double>(tr->qty);
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

AnalysisReport Report::run(const std::string& bin_path,
                           const std::vector<AgentId>& mm_ids) {
    AnalysisReport rep;

    LogData d = extract(bin_path, mm_ids);
    rep.n_trades    = d.n_trades;
    rep.n_snapshots = d.n_snapshots;

    if (d.mid_prices.size() < 100) {
        // Not enough data — mark all as failed.
        for (int i = 1; i <= 8; ++i)
            rep.results.push_back({"Fact " + std::to_string(i), false, 0.0, 0.0, 0.0, "insufficient data"});
        rep.total_count = 8;
        return rep;
    }

    std::vector<double> rets = log_returns(d.mid_prices);
    rep.n_returns = rets.size();
    int N = static_cast<int>(rets.size());

    // ── ACF of returns and |returns| ──────────────────────────────────────────
    constexpr int kMaxLag = 20;
    auto acf_ret = AcfComputer::compute(rets, kMaxLag);
    auto acf_abs = AcfComputer::compute_abs(rets, kMaxLag);
    double ci = acf_ret.confidence_band();  // ±2/sqrt(N), same for abs ACF

    // ── Ljung-Box Q(10) and multi-k Hill (extra diagnostics) ──────────────────
    rep.ljung_box_df = 10;
    rep.ljung_box_q  = ljung_box_q(acf_ret.acf, N, rep.ljung_box_df);
    rep.ljung_box_p  = chi2_sf(rep.ljung_box_q, rep.ljung_box_df);
    hill_multi_k(rets, rep.hill_alpha_mean, rep.hill_alpha_std);

    // ── Trade sign ACF (order-splitting indicator) ─────────────────────────────
    if (d.trade_signs.size() > static_cast<size_t>(kMaxLag + 2)) {
        auto acf_sign = AcfComputer::compute(d.trade_signs, kMaxLag);
        rep.trade_sign_acf1 = (acf_sign.acf.size() > 1) ? acf_sign.acf[1] : 0.0;
    }

    // ── Volume-volatility correlation ──────────────────────────────────────────
    if (rets.size() == d.volume_per_snap.size() - 1 && !rets.empty()) {
        // align: rets[i] = log(price[i+1]/price[i]), volume[i+1] = volume since price[i]
        std::vector<double> abs_rets(rets.size());
        std::transform(rets.begin(), rets.end(), abs_rets.begin(),
                       [](double r) { return std::abs(r); });
        std::vector<double> vols(d.volume_per_snap.begin() + 1, d.volume_per_snap.end());
        rep.vol_vol_corr = pearson_corr(abs_rets, vols);
    }

    // ── Fact 1: Fat tails (excess kurtosis > 1.0) ─────────────────────────────
    {
        double kurt = kurtosis_excess(rets);
        rep.results.push_back(make(
            "Fat tails (kurtosis)",
            kurt > 1.0,
            kurt, 1.0,
            "excess kurtosis of log-returns > 1.0"));
    }

    // ── Fact 2: Return ACF ≈ 0 at lags 1 AND 2 ───────────────────────────────
    // Both lags are tested: a market that merely pushes its momentum from lag 1
    // to lag 2 has not removed the arbitrage, only relabelled it.  The reported
    // value is the larger of the two absolute autocorrelations.
    {
        double a1 = acf_ret.acf.size() > 1 ? std::abs(acf_ret.acf[1]) : 1.0;
        double a2 = acf_ret.acf.size() > 2 ? std::abs(acf_ret.acf[2]) : 1.0;
        double val = std::max(a1, a2);
        constexpr double kAcfThreshold = 0.05;
        rep.results.push_back(make(
            "Return ACF ≈ 0 (lags 1,2)",
            val < kAcfThreshold,
            val, kAcfThreshold,
            "max(|ACF(r,1)|,|ACF(r,2)|) < 0.05 — no exploitable serial correlation",
            ci));
    }

    // ── Fact 3: Volatility clustering (ACF|r| lag=1 > 0.05) ──────────────────
    {
        double val = acf_abs.acf.size() > 1 ? acf_abs.acf[1] : 0.0;
        rep.results.push_back(make(
            "Vol clustering (ACF|r| lag=1)",
            val > 0.05,
            val, 0.05,
            "ACF(|r|, lag=1) > 0.05 — volatility is serially correlated",
            ci));
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
            "mean ACF(|r|, lag=1..10) > 0.02 — slow decay of volatility memory",
            ci));
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

    // ── Fact 6: Hill tail index — multi-k mean α ∈ [1.8, 6.0] ───────────────
    {
        double val = rep.hill_alpha_mean;
        bool   ok  = (val >= 1.8 && val <= 6.0 && rep.hill_alpha_std < val * 0.5);
        if (val == 0.0) {  // fallback to single-k if multi-k failed
            auto hill = HillEstimator::estimate_abs(rets);
            val = hill.valid ? hill.alpha : 0.0;
            ok  = hill.valid && val >= 1.8 && val <= 6.0;
        }
        rep.results.push_back(make(
            "Hill tail index α ∈ [1.8, 6.0]",
            ok,
            val, 1.8,
            "multi-k Hill estimator mean α in typical financial range; std/mean < 0.5"));
    }

    // ── Fact 7: Spread variation (CoV(spread) > 0.08) ─────────────────────────
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

    // ── Price discovery (diagnostics, not pass/fail) ──────────────────────────
    {
        auto& pd = rep.pd;
        pd.two_sided_frac = d.mid_prices.empty()
            ? 0.0 : static_cast<double>(d.n_two_sided) / d.mid_prices.size();
        {
            size_t zeros = 0;
            for (double x : rets) if (x == 0.0) ++zeros;
            pd.zero_ret_frac = rets.empty() ? 0.0
                : static_cast<double>(zeros) / rets.size();
        }
        pd.mm_self_trade_frac = (d.total_volume > 0.0)
            ? d.mm_volume / d.total_volume : 0.0;

        pd.has_fundamental = d.has_fundamental;
        if (pd.has_fundamental) {
            pd.corr_h1   = horizon_corr(d.fundamental, d.mid_prices, 1);
            pd.corr_h5   = horizon_corr(d.fundamental, d.mid_prices, 5);
            pd.corr_h20  = horizon_corr(d.fundamental, d.mid_prices, 20);
            pd.corr_h100 = horizon_corr(d.fundamental, d.mid_prices, 100);

            std::vector<double> gap;
            gap.reserve(d.mid_prices.size());
            for (size_t i = 0; i < d.mid_prices.size(); ++i)
                if (d.fundamental[i] > 0.0 && d.mid_prices[i] > 0.0)
                    gap.push_back(std::log(d.mid_prices[i] / d.fundamental[i]));
            if (!gap.empty()) {
                pd.gap_mean = mean_of(gap);
                pd.gap_std  = std::sqrt(variance_of(gap, pd.gap_mean));
                double rho  = ar1_rho(gap);
                pd.gap_half_life = (rho > 0.0 && rho < 1.0)
                    ? std::log(0.5) / std::log(rho) : 0.0;
            }
        }
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

    os << "| # | Test                              | Value     | Threshold |  CI ±  | Pass |\n";
    os << "|---|-----------------------------------|-----------|-----------|--------|------|\n";
    int idx = 1;
    for (const auto& r : results) {
        char row[300];
        std::snprintf(row, sizeof(row),
            "| %d | %-33s | %9.4f | %9.4f | %6.4f | %s  |\n",
            idx++,
            r.name.c_str(),
            r.value, r.threshold,
            r.ci_band,
            r.passed ? "PASS" : "FAIL");
        os << row;
    }
    os << "\nResult: " << passed_count << "/" << total_count << " facts validated\n";

    // Extra diagnostics
    os << "\nExtra diagnostics:\n";
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  Ljung-Box Q(%d)     = %.2f  (p = %.4g; p < 0.05 → autocorrelated returns)\n",
            ljung_box_df, ljung_box_q, ljung_box_p);
        os << buf;
        std::snprintf(buf, sizeof(buf),
            "  Hill α multi-k      = %.3f ± %.3f  (stability: std/mean = %.2f)\n",
            hill_alpha_mean, hill_alpha_std,
            (hill_alpha_mean > 0.0) ? hill_alpha_std / hill_alpha_mean : 0.0);
        os << buf;
        std::snprintf(buf, sizeof(buf),
            "  Trade sign ACF(1)   = %.4f  (>0 → order splitting / herding)\n",
            trade_sign_acf1);
        os << buf;
        std::snprintf(buf, sizeof(buf),
            "  Vol-volume corr     = %.4f  (>0 → volume predicts volatility)\n",
            vol_vol_corr);
        os << buf;
    }

    // ── Price discovery ───────────────────────────────────────────────────────
    os << "\nPrice discovery:\n";
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "  Two-sided book      = %6.2f%%  (target >= 99%%)\n",
            100.0 * pd.two_sided_frac);
        os << buf;
        std::snprintf(buf, sizeof(buf),
            "  Zero returns        = %6.2f%%  (a frozen mid is not a price)\n",
            100.0 * pd.zero_ret_frac);
        os << buf;
        std::snprintf(buf, sizeof(buf),
            "  MM-vs-MM volume     = %6.2f%%  (target < 5%%)\n",
            100.0 * pd.mm_self_trade_frac);
        os << buf;
        if (!pd.has_fundamental) {
            os << "  corr(V, mid)        = n/a (log has no fundamental_value; schema < v3)\n";
        } else {
            std::snprintf(buf, sizeof(buf),
                "  corr(rV, r_mid)     = %.3f (h=1)  %.3f (h=5)  %.3f (h=20)  %.3f (h=100)\n",
                pd.corr_h1, pd.corr_h5, pd.corr_h20, pd.corr_h100);
            os << buf;
            std::snprintf(buf, sizeof(buf),
                "  log(mid/V)          = %+.4f ± %.4f  (target std < 0.01)\n",
                pd.gap_mean, pd.gap_std);
            os << buf;
            if (pd.gap_half_life > 0.0)
                std::snprintf(buf, sizeof(buf),
                    "  gap half-life       = %6.1f ticks  (target < 50)\n", pd.gap_half_life);
            else
                std::snprintf(buf, sizeof(buf),
                    "  gap half-life       = no mean reversion (AR(1) rho outside (0,1))\n");
            os << buf;
        }
    }
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
