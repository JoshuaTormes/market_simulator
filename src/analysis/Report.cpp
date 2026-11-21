#include "analysis/Report.h"
#include "analysis/SeriesCollector.h"
#include "analysis/AcfComputer.h"
#include "analysis/HillEstimator.h"
#include "analysis/FlashCrashDetector.h"
#include "persistence/BinaryLogReader.h"
#include "persistence/EventSchema.h"
#include <algorithm>
#include <limits>
#include <map>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
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

// Attach a standard error, a 95% interval and z = (value − threshold)/se.
static constexpr double kZ95 = 1.959964;
static void annotate(StyleResult& r, char dir, double se, double lo, double hi,
                     const char* method) {
    r.dir = dir; r.se = se; r.ci_lo = lo; r.ci_hi = hi; r.se_method = method;
    r.z = (se > 0.0) ? (r.value - r.threshold) / se : 0.0;
}
static void annotate_se(StyleResult& r, char dir, double se, const char* method) {
    annotate(r, dir, se, r.value - kZ95 * se, r.value + kZ95 * se, method);
}
static void annotate_boot(StyleResult& r, char dir, sfv2::Interval iv) {
    annotate(r, dir, (iv.hi - iv.lo) / (2.0 * kZ95), iv.lo, iv.hi, "bootstrap");
}

static StyleResult make(const std::string& name,
                        bool passed,
                        double value,
                        double threshold,
                        const std::string& desc,
                        double ci_band = 0.0) {
    StyleResult r;
    r.name = name; r.passed = passed; r.value = value; r.threshold = threshold;
    r.ci_band = ci_band; r.description = desc;
    return r;
}

// ── data extraction ───────────────────────────────────────────────────────────

LogData Report::extract(const std::string& bin_path,
                        const std::vector<AgentId>& mm_ids) {
    BinaryLogReader r(bin_path);
    if (!r.read_header()) return LogData{};
    SeriesCollector c(mm_ids);
    while (auto rec = r.next()) c.consume(*rec);
    return c.finish();
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

// Annualized volatility from non-overlapping h-step log returns.
static double annualized_vol(const std::vector<double>& px, size_t h,
                             double ticks_per_year) {
    if (h == 0 || px.size() < 4 * h) return 0.0;
    std::vector<double> r;
    for (size_t i = h; i < px.size(); i += h)
        if (px[i] > 0.0 && px[i - h] > 0.0) r.push_back(std::log(px[i] / px[i - h]));
    if (r.size() < 4) return 0.0;
    const double m = mean_of(r);
    return std::sqrt(variance_of(r, m)) * std::sqrt(ticks_per_year / static_cast<double>(h));
}

// Price impact curve: E[sign(OFI_t) · (mid_{t+1} - mid_t)] per geometric bin of
// |OFI_t|, and the log-log slope across bins.  Linear (Kyle) impact gives a
// slope of 1; the empirical square-root law gives ~0.5.
static void impact_curve(const std::vector<double>& mid, const std::vector<double>& ofi,
                         double& exponent, double& at_50, int& n_bins) {
    exponent = at_50 = 0.0; n_bins = 0;
    const size_t n = std::min(mid.size(), ofi.size());
    if (n < 1000) return;
    constexpr int kBins = 12;                     // [1,2), [2,4), ... [2048, 4096)
    double sum_imp[kBins] = {0}, sum_q[kBins] = {0};
    long   cnt[kBins] = {0};
    for (size_t t = 0; t + 1 < n; ++t) {
        const double q = std::abs(ofi[t]);
        if (q < 1.0) continue;
        int b = static_cast<int>(std::floor(std::log2(q)));
        if (b >= kBins) b = kBins - 1;
        const double sgn = (ofi[t] > 0.0) ? 1.0 : -1.0;
        sum_imp[b] += sgn * (mid[t + 1] - mid[t]);
        sum_q[b]   += q;
        ++cnt[b];
    }
    std::vector<double> lx, ly;
    for (int b = 0; b < kBins; ++b) {
        if (cnt[b] < 50) continue;
        const double imp = sum_imp[b] / cnt[b];
        const double q   = sum_q[b] / cnt[b];
        if (imp <= 0.0) continue;
        lx.push_back(std::log(q));
        ly.push_back(std::log(imp));
        if (b == 5) at_50 = imp;                  // bin [32, 64) holds the 50-lot order
    }
    n_bins = static_cast<int>(lx.size());
    if (n_bins < 3) return;
    const double mx = mean_of(lx), my = mean_of(ly);
    double sxy = 0.0, sxx = 0.0;
    for (size_t i = 0; i < lx.size(); ++i) {
        sxy += (lx[i] - mx) * (ly[i] - my);
        sxx += (lx[i] - mx) * (lx[i] - mx);
    }
    exponent = (sxx > 0.0) ? sxy / sxx : 0.0;
}

// corr of h-step log changes of two log-level series, over non-overlapping
// blocks.  A block is skipped when an endpoint is not finite, or when any tick
// inside it is flagged in `drop` (prefix count `drop_cum`, size n + 1).
static double horizon_corr_log(const std::vector<double>& la, const std::vector<double>& lb,
                               size_t h, const std::vector<int>* drop_cum = nullptr) {
    const size_t n = std::min(la.size(), lb.size());
    if (h == 0 || n < 4 * h) return 0.0;
    std::vector<double> ra, rb;
    for (size_t i = h; i < n; i += h) {
        if (!std::isfinite(la[i]) || !std::isfinite(la[i - h]) ||
            !std::isfinite(lb[i]) || !std::isfinite(lb[i - h])) continue;
        if (drop_cum && (*drop_cum)[i + 1] - (*drop_cum)[i - h] > 0) continue;
        ra.push_back(la[i] - la[i - h]);
        rb.push_back(lb[i] - lb[i - h]);
    }
    return pearson_corr(ra, rb);
}

static std::vector<double> log_or_nan(const std::vector<double>& x) {
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] > 0.0 ? std::log(x[i]) : std::numeric_limits<double>::quiet_NaN();
    return y;
}

// Price discovery by component of log V, and tracking away from the news.
static void price_discovery_components(const LogData& d, const TimeScale& ts, PriceDiscovery& pd) {
    if (!d.has_fundamental || !d.has_news_cum || d.news_cum.size() != d.fundamental.size()) return;
    const size_t n = std::min(d.mid_prices.size(), d.fundamental.size());
    if (n < 400) return;
    pd.has_components = true;
    const auto lm = log_or_nan(d.mid_prices);
    const auto lv = log_or_nan(d.fundamental);
    std::vector<double> ldiff(n), lnews(n);
    for (size_t i = 0; i < n; ++i) { lnews[i] = d.news_cum[i]; ldiff[i] = lv[i] - d.news_cum[i]; }
    static const size_t H[4] = {1, 5, 20, 100};
    for (int k = 0; k < 4; ++k) {
        pd.corr_diff[k] = horizon_corr_log(ldiff, lm, H[k]);
        pd.corr_news[k] = horizon_corr_log(lnews, lm, H[k]);
    }
    // News ticks: where the cumulative announced return changes.
    const double w = 60.0 / ts.seconds_per_tick;
    std::vector<uint64_t> news_t;
    for (size_t i = 1; i < n; ++i)
        if (d.news_cum[i] != d.news_cum[i - 1]) news_t.push_back(d.price_ticks[i]);
    pd.n_news = static_cast<int>(news_t.size());
    std::vector<int> cum(n + 1, 0);
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(d.price_ticks[i]);
        while (j < news_t.size() && static_cast<double>(news_t[j]) + w < t) ++j;
        const bool near = j < news_t.size() && std::abs(static_cast<double>(news_t[j]) - t) <= w;
        cum[i + 1] = cum[i] + (near ? 1 : 0);
    }
    pd.excluded_frac = static_cast<double>(cum[n]) / static_cast<double>(n);
    pd.corr_h20_ex_news  = horizon_corr_log(lv, lm, 20, &cum);
    pd.corr_h100_ex_news = horizon_corr_log(lv, lm, 100, &cum);
    std::vector<double> gap;
    for (size_t i = 0; i < n; ++i)
        if (cum[i + 1] == cum[i] && std::isfinite(lm[i]) && std::isfinite(lv[i]))
            gap.push_back(lm[i] - lv[i]);
    if (gap.size() > 2) pd.gap_std_ex_news = std::sqrt(variance_of(gap, mean_of(gap)));
}

// Index of the last snapshot with tick <= t (0 if none).
static size_t snap_at(const std::vector<uint64_t>& ticks, uint64_t t) {
    auto it = std::upper_bound(ticks.begin(), ticks.end(), t);
    return it == ticks.begin() ? 0 : static_cast<size_t>(it - ticks.begin()) - 1;
}

// δ from geometric (factor 2) bins of x = Q/ADV: OLS slope of log mean I on
// log mean x, over bins with >= kMinPerBin parents and a positive mean impact.
static constexpr int kMinPerBin = 10;
static bool fit_impact(const std::vector<double>& x, const std::vector<double>& imp,
                       const std::vector<size_t>& idx, double x_min,
                       double& delta, std::vector<MetaorderImpact::Bin>* bins_out) {
    constexpr int kB = 16;
    double sx[kB] = {0}, si[kB] = {0}; int c[kB] = {0};
    for (size_t k : idx) {
        int b = static_cast<int>(std::floor(std::log2(x[k] / x_min)));
        b = std::clamp(b, 0, kB - 1);
        sx[b] += x[k]; si[b] += imp[k]; ++c[b];
    }
    std::vector<double> lx, ly;
    for (int b = 0; b < kB; ++b) {
        if (c[b] < kMinPerBin) continue;
        const double mi = si[b] / c[b], mx = sx[b] / c[b];
        if (bins_out) bins_out->push_back({mx, 1e4 * mi, c[b]});
        if (mi <= 0.0) continue;
        lx.push_back(std::log(mx)); ly.push_back(std::log(mi));
    }
    if (lx.size() < 3) return false;
    const double mx = mean_of(lx), my = mean_of(ly);
    double sxy = 0.0, sxx = 0.0;
    for (size_t i = 0; i < lx.size(); ++i) { sxy += (lx[i] - mx) * (ly[i] - my); sxx += (lx[i] - mx) * (lx[i] - mx); }
    if (sxx <= 0.0) return false;
    delta = sxy / sxx;
    return true;
}

static void metaorder_impact(const LogData& d, const TimeScale& ts, MetaorderImpact& mo) {
    if (d.metaorders.empty() || d.price_ticks.size() < 100) return;
    const double days = static_cast<double>(d.price_ticks.back() + 1) / ts.ticks_per_day();
    mo.adv = days > 0.0 ? d.total_volume / days : 0.0;
    {
        const auto r5 = sfv2::aggregate(d, static_cast<int>(std::lround(300.0 / ts.seconds_per_tick)), ts);
        if (r5.ret.size() > 10) {
            const double m = mean_of(r5.ret);
            mo.sigma_daily = std::sqrt(variance_of(r5.ret, m) * ts.ticks_per_day() / r5.horizon_ticks);
        }
    }
    if (mo.adv <= 0.0) return;
    const uint64_t last = d.price_ticks.back();
    std::vector<double> x, imp, perm;
    std::vector<char> has_perm;
    for (const auto& m : d.metaorders) {
        if (m.qty_executed <= 0.0 || m.tick_end < m.tick_start) continue;
        const size_t i0 = snap_at(d.price_ticks, m.tick_start > 0 ? m.tick_start - 1 : 0);
        const size_t i1 = snap_at(d.price_ticks, m.tick_end);
        const double p0 = d.mid_prices[i0], p1 = d.mid_prices[i1];
        if (p0 <= 0.0 || p1 <= 0.0) continue;
        x.push_back(m.qty_executed / mo.adv);
        imp.push_back(m.sign * std::log(p1 / p0));
        const uint64_t t2 = m.tick_end + (m.tick_end - m.tick_start);
        const double p2 = t2 <= last ? d.mid_prices[snap_at(d.price_ticks, t2)] : 0.0;
        has_perm.push_back(p2 > 0.0);
        perm.push_back(p2 > 0.0 ? m.sign * std::log(p2 / p0) : 0.0);
    }
    mo.n = static_cast<int>(x.size());
    if (mo.n == 0) return;
    double st = 0.0, sp = 0.0, sy_num = 0.0, sy_den = 0.0;
    for (size_t k = 0; k < x.size(); ++k) {
        sy_num += imp[k]; sy_den += mo.sigma_daily * std::sqrt(x[k]);
        if (has_perm[k]) { st += imp[k]; sp += perm[k]; ++mo.n_perm; }
    }
    mo.y_prefactor = sy_den > 0.0 ? sy_num / sy_den : 0.0;
    if (mo.n_perm > 0) {
        mo.temp_mean_bp = 1e4 * st / mo.n_perm;
        mo.perm_mean_bp = 1e4 * sp / mo.n_perm;
        mo.perm_over_temp = st != 0.0 ? sp / st : 0.0;
    }
    const double x_min = *std::min_element(x.begin(), x.end());
    std::vector<size_t> all(x.size());
    std::iota(all.begin(), all.end(), size_t{0});
    mo.delta_valid = fit_impact(x, imp, all, x_min, mo.delta, &mo.bins);
    if (!mo.delta_valid) return;
    // Bootstrap over parents (they are separated by ~500 ticks of idle time).
    std::mt19937_64 eng(0x5F2Bu);
    std::uniform_int_distribution<size_t> pick(0, x.size() - 1);
    std::vector<double> ds;
    constexpr int kBoot = 400;
    std::vector<size_t> idx(x.size());
    for (int b = 0; b < kBoot; ++b) {
        for (auto& k : idx) k = pick(eng);
        double dl = 0.0;
        if (fit_impact(x, imp, idx, x_min, dl, nullptr)) ds.push_back(dl);
    }
    mo.boot_valid_frac = static_cast<double>(ds.size()) / kBoot;
    if (ds.size() >= 20) {
        std::sort(ds.begin(), ds.end());
        mo.delta_lo = ds[static_cast<size_t>(0.025 * double(ds.size() - 1) + 0.5)];
        mo.delta_hi = ds[static_cast<size_t>(0.975 * double(ds.size() - 1) + 0.5)];
    }
}

// Intraday U: mean volume per tick in the first and last half hour of each
// session over the mean between 1h30 and 5h into the session.  Needs at least
// one complete session.
static double intraday_u_ratio(const std::vector<uint64_t>& ticks,
                               const std::vector<double>& vol, double ticks_per_day,
                               bool positive_only = false) {
    if (ticks.empty() || ticks_per_day <= 0.0) return 0.0;
    if (static_cast<double>(ticks.back()) < ticks_per_day) return 0.0;
    double edge_v = 0.0, mid_v = 0.0;
    long   edge_n = 0,   mid_n = 0;
    const double half_hour = 1800.0 / 23400.0 * ticks_per_day;
    const double core_lo   = 5400.0 / 23400.0 * ticks_per_day;
    const double core_hi   = 18000.0 / 23400.0 * ticks_per_day;
    for (size_t i = 0; i < ticks.size() && i < vol.size(); ++i) {
        if (positive_only && !(vol[i] > 0.0)) continue;
        const double tod = std::fmod(static_cast<double>(ticks[i]), ticks_per_day);
        if (tod < half_hour || tod >= ticks_per_day - half_hour) { edge_v += vol[i]; ++edge_n; }
        else if (tod >= core_lo && tod < core_hi)                { mid_v  += vol[i]; ++mid_n; }
    }
    if (edge_n == 0 || mid_n == 0 || mid_v <= 0.0) return 0.0;
    return (edge_v / edge_n) / (mid_v / mid_n);
}

// ── Report::run ───────────────────────────────────────────────────────────────

AnalysisReport Report::run(const std::string& bin_path,
                           const std::vector<AgentId>& mm_ids,
                           const TimeScale& ts) {
    return compute(extract(bin_path, mm_ids), ts);
}

AnalysisReport Report::compute(const LogData& d, const TimeScale& ts) {
    AnalysisReport rep;
    rep.n_trades    = d.n_trades;
    rep.n_snapshots = d.n_snapshots;

    if (d.mid_prices.size() < 100) {
        // Not enough data — mark all as failed.
        for (int i = 1; i <= 8; ++i)
            rep.results.push_back(make("Fact " + std::to_string(i), false, 0.0, 0.0, "insufficient data"));
        rep.total_count = 8;
        return rep;
    }

    std::vector<double> rets = log_returns(d.mid_prices);
    rep.n_returns = rets.size();
    int N = static_cast<int>(rets.size());
    std::vector<double> abs_r(rets.size());
    for (size_t i = 0; i < rets.size(); ++i) abs_r[i] = std::abs(rets[i]);
    constexpr int kBootLegacy = 200;

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
        annotate_boot(rep.results.back(), '>',
                      sfv2::block_bootstrap(rets, kurtosis_excess, kBootLegacy));
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
        annotate_se(rep.results.back(), '<', sfv2::acf_robust_se(rets, a1 >= a2 ? 1 : 2), "robusta");
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
        annotate_se(rep.results.back(), '>', sfv2::acf_robust_se(abs_r, 1), "robusta");
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
        static const std::vector<int> lags10 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        annotate_boot(rep.results.back(), '>', sfv2::block_bootstrap(abs_r,
            [](const std::vector<double>& x) {
                const auto a = sfv2::acf_at(x, lags10);
                return std::accumulate(a.begin(), a.end(), 0.0) / static_cast<double>(a.size());
            }, kBootLegacy));
    }

    // ── Fact 5: Gain-loss asymmetry (skewness < 0) ────────────────────────────
    {
        double skew = skewness(rets);
        rep.results.push_back(make(
            "Gain-loss asymmetry (skew < 0)",
            skew < 0.0,
            skew, 0.0,
            "skewness(r) < 0 — losses larger than gains"));
        annotate_boot(rep.results.back(), '<', sfv2::block_bootstrap(rets, skewness, kBootLegacy));
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
        // Asymptotic Hill SE α/√k at the middle k = √n of the multi-k grid.
        auto& r6 = rep.results.back();
        const double k_mid = std::ceil(std::sqrt(static_cast<double>(N)));
        annotate_se(r6, 'r', k_mid > 0.0 ? val / std::sqrt(k_mid) : 0.0, "assintótica");
        r6.z = r6.se > 0.0 ? std::min(val - 1.8, 6.0 - val) / r6.se : 0.0;
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
        annotate_boot(rep.results.back(), '>', sfv2::block_bootstrap(pos_spreads,
            [](const std::vector<double>& x) {
                const double m = mean_of(x);
                return m > 0.0 ? std::sqrt(variance_of(x, m)) / m : 0.0;
            }, kBootLegacy));
    }

    // ── Fact 8: OFI clustering (ACF(OFI, lag=1) > 0.03) ─────────────────────
    {
        double val = 0.0;
        if (d.ofi.size() > static_cast<size_t>(kMaxLag + 2)) {
            auto acf_ofi = AcfComputer::compute(d.ofi, kMaxLag);
            if (acf_ofi.acf.size() > 1) {
                val = std::abs(acf_ofi.acf[1]);
                rep.ofi_acf1_signed = acf_ofi.acf[1];
            }
        }
        rep.results.push_back(make(
            "OFI clustering (ACF|OFI| lag=1 > 0.03)",
            val > 0.03,
            val, 0.03,
            "order-flow imbalance shows serial correlation — flow toxicity clusters"));
        annotate_se(rep.results.back(), '>', d.ofi.size() > 2 ? sfv2::acf_robust_se(d.ofi, 1) : 0.0,
                    "robusta");
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

    price_discovery_components(d, ts, rep.pd);
    metaorder_impact(d, ts, rep.metaorder);

    // ── Realism diagnostics ───────────────────────────────────────────────────
    {
        auto& rl = rep.realism;
        const double tpy = ts.ticks_per_year();
        const size_t one_min = static_cast<size_t>(std::max(1.0, std::round(60.0 / ts.seconds_per_tick)));
        rl.ann_vol_mid_1m  = annualized_vol(d.mid_prices, one_min, tpy);
        if (d.has_fundamental) rl.ann_vol_fund_1m = annualized_vol(d.fundamental, one_min, tpy);
        const double v1 = annualized_vol(d.mid_prices, 1, tpy);
        rl.signature_ratio = (rl.ann_vol_mid_1m > 0.0) ? v1 / rl.ann_vol_mid_1m : 0.0;
        impact_curve(d.mid_prices, d.ofi, rl.impact_exponent, rl.impact_ticks_50, rl.impact_bins);
        rl.intraday_u_ratio = intraday_u_ratio(d.price_ticks, d.volume_per_snap, ts.ticks_per_day());
        {
            double sp_sum = 0.0, px_sum = 0.0; long k = 0;
            for (size_t i = 0; i < d.spreads.size(); ++i)
                if (d.spreads[i] > 0.0) { sp_sum += d.spreads[i]; px_sum += d.mid_prices[i]; ++k; }
            rl.mean_spread_bp = (k > 0 && px_sum > 0.0) ? 1e4 * sp_sum / px_sum : 0.0;
        }
        if (static_cast<double>(d.price_ticks.back()) >= ts.ticks_per_day()) {
            std::vector<double> sp_bp(d.spreads.size(), 0.0);
            for (size_t i = 0; i < d.spreads.size(); ++i)
                if (d.spreads[i] > 0.0 && d.mid_prices[i] > 0.0) sp_bp[i] = 1e4 * d.spreads[i] / d.mid_prices[i];
            rl.intraday_u_spread = intraday_u_ratio(d.price_ticks, sp_bp, ts.ticks_per_day(), true);
            // |r| at 1 min: open+close half-hours over 1h30–5h, by slot.
            const int m1 = static_cast<int>(std::lround(60.0 / ts.seconds_per_tick));
            const auto r1 = sfv2::aggregate(d, m1, ts);
            const double spd = ts.ticks_per_day() / m1;
            double e = 0.0, c = 0.0; long ne = 0, nc = 0;
            for (size_t i = 0; i < r1.ret.size(); ++i) {
                const double sl = r1.slot[i] / spd;   // fraction of the session
                if (sl < 1800.0 / 23400.0 || sl >= 1.0 - 1800.0 / 23400.0) { e += std::abs(r1.ret[i]); ++ne; }
                else if (sl >= 5400.0 / 23400.0 && sl < 18000.0 / 23400.0) { c += std::abs(r1.ret[i]); ++nc; }
            }
            rl.intraday_u_absr = (ne > 0 && nc > 0 && c > 0.0) ? (e / ne) / (c / nc) : 0.0;
        }
        {
            double tot = 0.0, top = 0.0;
            for (const auto& [id, v] : d.volume_by_mm) { tot += v; top = std::max(top, v); }
            rl.mm_top_share = (tot > 0.0) ? top / tot : 0.0;
        }
        if (d.non_mm_top_n > 0)
            rl.non_mm_top_depth_frac = d.non_mm_top_sum / static_cast<double>(d.non_mm_top_n);
    }

    // ── Flash crash detection (informational only) ────────────────────────────
    {
        // A drop of at least 1% and 5 local sigmas inside a minute, half of it
        // recovered within five minutes.  Anything looser counts tick noise.
        FlashCrashDetector::Config fcc{5.0, 60, 300, 0.50, 300, 0.01};
        auto evts = FlashCrashDetector::detect(d.mid_prices, d.price_ticks, fcc);
        rep.flash_crashes = static_cast<int>(evts.size());
    }

    rep.v2 = sfv2::compute(d, ts);

    rep.total_count = static_cast<int>(rep.results.size());
    for (const auto& r2 : rep.results) rep.passed_count += r2.passed ? 1 : 0;
    return rep;
}

// ── Formatting ────────────────────────────────────────────────────────────────

std::string AnalysisReport::to_text() const {
    std::ostringstream os;
    char buf[400];
    const auto line = [&](const char* f, auto... a) {
        std::snprintf(buf, sizeof(buf), f, a...);
        os << buf;
    };

    os << "Relatório de fatos estilizados\n";
    os << "==============================\n";
    os << "Amostras: " << n_returns << " retornos  "
       << n_trades << " trades  "
       << n_snapshots << " snapshots\n";
    os << "Flash crashes detectados: " << flash_crashes << "\n\n";

    // ── Legacy battery ────────────────────────────────────────────────────────
    os << "Bateria legada (1 s)\n";
    os << "--------------------\n";
    os << "Definições e limiares originais, intocados. SE: robusta = Taylor/Lo-MacKinlay;\n"
          "bootstrap = blocos móveis; assintótica = α/√k. z = (valor − limiar)/SE; no Hill,\n"
          "distância ao limite mais próximo da faixa.\n\n";
    os << "| # | Teste                                  |     Valor | Limiar     |     SE    | IC 95%                  |       z | Passa |\n";
    os << "|---|----------------------------------------|-----------|------------|-----------|-------------------------|---------|-------|\n";
    int idx = 1;
    for (const auto& r : results) {
        char thr[32];
        if (r.dir == 'r') std::snprintf(thr, sizeof thr, "[1.8; 6.0]");
        else              std::snprintf(thr, sizeof thr, "%c %.4f", r.dir, r.threshold);
        std::string name = r.name;
        for (size_t p = 0; (p = name.find('|', p)) != std::string::npos; p += 2) name.insert(p, "\\");
        line("| %d | %-38s | %9.4f | %-10s | %9.4f | [%9.4f; %9.4f] | %7.2f | %s |\n",
             idx++, name.c_str(), r.value, thr, r.se, r.ci_lo, r.ci_hi, r.z,
             r.passed ? "PASSA" : "FALHA");
    }
    os << "\nResultado legado: " << passed_count << "/" << total_count << " fatos\n";

    os << "\nDiagnósticos extras (1 s):\n";
    line("  Ljung-Box Q(%d)      = %.2f  (p = %.4g; p < 0,05 → retornos autocorrelacionados)\n",
         ljung_box_df, ljung_box_q, ljung_box_p);
    line("  Hill α multi-k       = %.3f ± %.3f  (estabilidade: dp/média = %.2f)\n",
         hill_alpha_mean, hill_alpha_std,
         (hill_alpha_mean > 0.0) ? hill_alpha_std / hill_alpha_mean : 0.0);
    line("  ACF(sinal de trade,1)= %.4f  (> 0 → fatiamento de ordens / manada)\n", trade_sign_acf1);
    line("  corr(|r|, volume)    = %.4f\n", vol_vol_corr);
    line("  ACF(OFI, 1) com sinal= %+.4f  (o Fato 8 testa o valor absoluto)\n", ofi_acf1_signed);

    // ── v2 battery ────────────────────────────────────────────────────────────
    os << "\n" << v2.to_text();

    // ── Price discovery ───────────────────────────────────────────────────────
    os << "\nPrice discovery\n";
    os << "---------------\n";
    line("  Book com dois lados  = %6.2f%%  (alvo >= 99%%)\n", 100.0 * pd.two_sided_frac);
    line("  Retornos nulos       = %6.2f%%  (mid congelado não é preço)\n", 100.0 * pd.zero_ret_frac);
    line("  Volume MM contra MM  = %6.2f%%  (alvo < 5%%)\n", 100.0 * pd.mm_self_trade_frac);
    if (!pd.has_fundamental) {
        os << "  corr(V, mid)         = n/d (log sem fundamental_value; schema < v3)\n";
    } else {
        line("  corr(rV, r_mid)      = %.3f (h=1)  %.3f (h=5)  %.3f (h=20)  %.3f (h=100)\n",
             pd.corr_h1, pd.corr_h5, pd.corr_h20, pd.corr_h100);
        if (pd.has_components) {
            line("    só a difusão       = %.3f (h=1)  %.3f (h=5)  %.3f (h=20)  %.3f (h=100)\n",
                 pd.corr_diff[0], pd.corr_diff[1], pd.corr_diff[2], pd.corr_diff[3]);
            line("    só as notícias     = %.3f (h=1)  %.3f (h=5)  %.3f (h=20)  %.3f (h=100)\n",
                 pd.corr_news[0], pd.corr_news[1], pd.corr_news[2], pd.corr_news[3]);
        } else {
            os << "    por componente     = n/d (log sem news_cum_log; schema < v4)\n";
        }
        line("  log(mid/V)           = %+.4f ± %.4f  (alvo dp < 0,01)\n", pd.gap_mean, pd.gap_std);
        if (pd.gap_half_life > 0.0)
            line("  meia-vida do gap     = %6.1f ticks  (alvo < 50)\n", pd.gap_half_life);
        else
            os << "  meia-vida do gap     = sem reversão à média (ρ do AR(1) fora de (0,1))\n";
        if (pd.has_components)
            line("  Longe das notícias   = %d notícias, %.1f%% dos ticks excluídos (±60 s): "
                 "dp(gap) %.4f, corr h=20 %.3f, h=100 %.3f\n",
                 pd.n_news, 100.0 * pd.excluded_frac, pd.gap_std_ex_news,
                 pd.corr_h20_ex_news, pd.corr_h100_ex_news);
    }

    // ── Metaorder impact ──────────────────────────────────────────────────────
    os << "\nImpacto de metaordem (parents institucionais concluídos)\n";
    os << "--------------------------------------------------------\n";
    const auto& mo = metaorder;
    if (mo.n == 0) {
        os << "  n/d (log sem registros de metaordem; schema < v4, ou nenhuma concluída)\n";
    } else {
        line("  Parents: %d   ADV: %.0f lotes   σ diário (5 min): %.2f%%\n",
             mo.n, mo.adv, 100.0 * mo.sigma_daily);
        if (mo.delta_valid)
            line("  Expoente δ           = %.3f  IC 95%% [%.3f; %.3f]  (%.0f%% das réplicas ajustaram; "
                 "0,5 = lei da raiz quadrada)\n",
                 mo.delta, mo.delta_lo, mo.delta_hi, 100.0 * mo.boot_valid_frac);
        else
            os << "  Expoente δ           = n/d (menos de 3 bins com >= 10 parents e impacto positivo)\n";
        line("  Prefator Y           = %.3f  (I ≈ Y·σ_D·√(Q/ADV); empírico ~0,5–1)\n", mo.y_prefactor);
        line("  Temporário / permanente = %.2f bp / %.2f bp  (razão %.2f; n = %d)\n",
             mo.temp_mean_bp, mo.perm_mean_bp, mo.perm_over_temp, mo.n_perm);
        if (!mo.bins.empty()) {
            os << "  | Q/ADV    | I médio (bp) |   n |\n";
            for (const auto& b : mo.bins)
                line("  | %8.5f | %12.2f | %3d |\n", b.q_adv, b.impact_bp, b.count);
        }
    }

    // ── Rule diagnostic ───────────────────────────────────────────────────────
    os << "\nDiagnóstico de regra: resposta do maker ao OFI (eco de impact_q_ref)\n";
    os << "--------------------------------------------------------------------\n";
    os << "  Mede a regra de crença do maker, não um fato emergente: o 0,5 sai da raiz em\n"
          "  MarketMakerAS. O fato é o δ de metaordem acima.\n";
    line("  Expoente por tick    = %.2f em %d bins\n", realism.impact_exponent, realism.impact_bins);
    line("  Impacto de ~50 lotes = %.2f ticks no tick seguinte\n", realism.impact_ticks_50);

    // ── Calibration / mechanics ───────────────────────────────────────────────
    os << "\nChecagem de calibração / mecânica\n";
    os << "---------------------------------\n";
    os << "  Grandezas que entram por parâmetro; conferem a calibração, não validam o modelo.\n";
    line("  Vol anual 1 min      = mid %5.1f%%   fundamental %5.1f%%   (alvo: sigma_annual)\n",
         100.0 * realism.ann_vol_mid_1m, 100.0 * realism.ann_vol_fund_1m);
    line("  Assinatura 1s/1m     = %.2f  (> 1: bid-ask bounce / impacto defasado)\n",
         realism.signature_ratio);
    line("  Razão U de volume    = %.2f  (abertura+fechamento / meio do pregão; imposta pelo perfil de chegada)\n",
         realism.intraday_u_ratio);
    if (results.size() >= 7)
        line("  CoV do spread        = %.4f  (Fato legado 7)\n", results[6].value);
    line("  Spread médio         = %.2f bp\n", realism.mean_spread_bp);
    line("  Share do maior MM    = %5.1f%%  (alvo < 50%%)\n", 100.0 * realism.mm_top_share);
    if (realism.non_mm_top_depth_frac >= 0.0)
        line("  Topo fora dos makers = %5.1f%%  (profundidade no melhor bid+ask de não-makers)\n",
             100.0 * realism.non_mm_top_depth_frac);

    os << "\nDiagnósticos emergentes intradiários\n";
    os << "------------------------------------\n";
    line("  Razão U de |r| 1 min = %.2f  (não é parâmetro; herda em parte a chegada em U; empírico > 1)\n", realism.intraday_u_absr);
    line("  Razão U do spread    = %.2f  (não é parâmetro; empírico: spread maior na abertura)\n", realism.intraday_u_spread);
    return os.str();
}

void AnalysisReport::to_csv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "rank,name,value,threshold,passed,se,ci_lo,ci_hi,z,description\n");
    int idx = 1;
    for (const auto& r : results) {
        std::fprintf(f, "%d,\"%s\",%.6f,%.6f,%s,%.6f,%.6f,%.6f,%.4f,\"%s\"\n",
            idx++, r.name.c_str(), r.value, r.threshold,
            r.passed ? "true" : "false", r.se, r.ci_lo, r.ci_hi, r.z, r.description.c_str());
    }
    std::fclose(f);
}
