#include "analysis/StylizedFactsV2.h"
#include "analysis/HillEstimator.h"
#include <cstdio>
#include <numeric>
#include <sstream>

namespace sfv2 {

const char* to_string(Status s) {
    switch (s) {
        case Status::Pass:         return "PASSA";
        case Status::Fail:         return "FALHA";
        case Status::Inconclusive: return "INCONCLUSIVO";
    }
    return "?";
}

// ── Primitives ─────────────────────────────────────────────────────────────

Aggregated aggregate(const LogData& d, int h, const TimeScale& ts) {
    Aggregated a;
    a.horizon_ticks = h;
    const int64_t D = static_cast<int64_t>(std::llround(ts.ticks_per_day()));
    const size_t  n = std::min(d.mid_prices.size(), d.price_ticks.size());
    if (h <= 0 || D <= 0 || n == 0) return a;

    struct Bucket { int64_t day, slot; double close, vol; };
    std::vector<Bucket> b;
    for (size_t i = 0; i < n; ++i) {
        const double m = d.mid_prices[i];
        if (!(m > 0.0)) continue;
        const int64_t t    = static_cast<int64_t>(d.price_ticks[i]);
        const int64_t day  = t / D;
        const int64_t slot = (t % D) / h;
        const double  v    = i < d.volume_per_snap.size() ? d.volume_per_snap[i] : 0.0;
        if (!b.empty() && b.back().day == day && b.back().slot == slot) {
            b.back().close = m;
            b.back().vol  += v;
        } else {
            b.push_back({day, slot, m, v});
        }
    }
    for (size_t i = 1; i < b.size(); ++i) {
        if (b[i].day != b[i - 1].day || b[i].slot != b[i - 1].slot + 1) continue;
        a.ret.push_back(std::log(b[i].close / b[i - 1].close));
        a.volume.push_back(b[i].vol);
        a.slot.push_back(static_cast<int>(b[i].slot));
        a.day.push_back(b[i].day);
    }
    return a;
}

double mean(const std::vector<double>& x) {
    if (x.empty()) return 0.0;
    return std::accumulate(x.begin(), x.end(), 0.0) / double(x.size());
}

static double central_moment(const std::vector<double>& x, double m, int p) {
    double s = 0.0;
    for (double v : x) s += std::pow(v - m, p);
    return s / double(x.size());
}

double excess_kurtosis(const std::vector<double>& x) {
    if (x.size() < 4) return 0.0;
    const double m  = mean(x);
    const double m2 = central_moment(x, m, 2);
    if (m2 <= 0.0) return 0.0;
    return central_moment(x, m, 4) / (m2 * m2) - 3.0;
}

double skewness(const std::vector<double>& x) {
    if (x.size() < 3) return 0.0;
    const double m  = mean(x);
    const double m2 = central_moment(x, m, 2);
    if (m2 <= 0.0) return 0.0;
    return central_moment(x, m, 3) / std::pow(m2, 1.5);
}

double pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = std::min(x.size(), y.size());
    if (n < 3) return 0.0;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= double(n); my /= double(n);
    double sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < n; ++i) {
        const double a = x[i] - mx, c = y[i] - my;
        sxy += a * c; sxx += a * a; syy += c * c;
    }
    return (sxx > 0 && syy > 0) ? sxy / std::sqrt(sxx * syy) : 0.0;
}

std::vector<double> acf_at(const std::vector<double>& x, const std::vector<int>& lags) {
    std::vector<double> out(lags.size(), 0.0);
    const size_t n = x.size();
    if (n < 3) return out;
    const double m = mean(x);
    std::vector<double> c(n);
    double var = 0.0;
    for (size_t i = 0; i < n; ++i) { c[i] = x[i] - m; var += c[i] * c[i]; }
    if (var <= 0.0) return out;
    for (size_t j = 0; j < lags.size(); ++j) {
        const size_t k = static_cast<size_t>(lags[j]);
        if (k >= n) continue;
        double s = 0.0;
        for (size_t i = k; i < n; ++i) s += c[i] * c[i - k];
        out[j] = s / var;
    }
    return out;
}

double acf_robust_se(const std::vector<double>& x, int lag) {
    const size_t n = x.size(), k = static_cast<size_t>(lag);
    if (n <= k + 2) return 1.0;
    const double m = mean(x);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = x[i] - m;
        den += a * a;
        if (i >= k) { const double b = x[i - k] - m; num += a * a * b * b; }
    }
    return den > 0.0 ? std::sqrt(num) / den : 1.0;
}

std::vector<int> geometric_lags(int k_max, double ratio) {
    std::vector<int> l;
    double k = 1.0;
    while (k <= k_max) {
        const int ki = static_cast<int>(std::lround(k));
        if (l.empty() || ki != l.back()) l.push_back(ki);
        k = std::max(k * ratio, k + 1.0);
    }
    return l;
}

static void ols(const std::vector<double>& x, const std::vector<double>& y,
                double& slope, double& icpt, double& rss) {
    const size_t n = x.size();
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= double(n); my /= double(n);
    double sxy = 0, sxx = 0;
    for (size_t i = 0; i < n; ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); }
    slope = sxx > 0 ? sxy / sxx : 0.0;
    icpt  = my - slope * mx;
    rss = 0.0;
    for (size_t i = 0; i < n; ++i) { const double e = y[i] - icpt - slope * x[i]; rss += e * e; }
}

DecayFit fit_decay(const std::vector<int>& lags, const std::vector<double>& acf) {
    DecayFit f;
    if (lags.empty() || lags.size() != acf.size()) return f;
    // Geometric bins; stop at the first bin whose mean ACF is not positive —
    // beyond it the estimate is noise around zero and a log cannot be taken.
    std::vector<double> lx, ly, kx;
    double lo = lags.front();
    size_t i = 0;
    while (i < lags.size()) {
        const double hi = lo * 1.25;
        double sr = 0, slk = 0, sk = 0; int c = 0;
        while (i < lags.size() && lags[i] < hi) {
            sr += acf[i]; slk += std::log(double(lags[i])); sk += lags[i]; ++c; ++i;
        }
        lo = hi;
        if (c == 0) continue;
        const double r = sr / c;
        if (!(r > 0.0)) break;
        lx.push_back(slk / c); kx.push_back(sk / c); ly.push_back(std::log(r));
    }
    f.bins = static_cast<int>(lx.size());
    if (f.bins < 5) return f;
    const double n = f.bins;
    double s, a, rss_p, rss_e;
    ols(lx, ly, s, a, rss_p); f.beta = -s;
    ols(kx, ly, s, a, rss_e); f.tau  = s < 0 ? -1.0 / s : 0.0;
    const auto aic = [&](double rss) { return n * std::log(std::max(rss, 1e-300) / n) + 4.0; };
    f.aic_power = aic(rss_p);
    f.aic_exp   = aic(rss_e);
    f.valid = true;
    return f;
}

// Intraday seasonality.  The arrival rates follow a U over the session, so
// raw |r| and volume share a deterministic daily pattern; left in, it shows up
// as volatility clustering, long memory and vol-volume correlation that no
// dynamics produced.  Each slot is divided by its mean over the sample
// (Andersen & Bollerslev 1997).  Needs several sessions to estimate a profile.
static constexpr int kMinDaysForProfile = 5;

static int n_days(const Aggregated& a) {
    int c = 0;
    for (size_t i = 0; i < a.day.size(); ++i)
        if (i == 0 || a.day[i] != a.day[i - 1]) ++c;
    return c;
}

static std::vector<double> deseasonalize(const std::vector<double>& x,
                                         const std::vector<int>& slot, bool apply) {
    if (!apply) return x;
    int smax = 0;
    for (int s : slot) smax = std::max(smax, s);
    std::vector<double> sum(static_cast<size_t>(smax) + 1, 0.0), cnt(sum.size(), 0.0);
    for (size_t i = 0; i < x.size(); ++i) { sum[slot[i]] += x[i]; cnt[slot[i]] += 1.0; }
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        const double prof = cnt[slot[i]] > 0 ? sum[slot[i]] / cnt[slot[i]] : 0.0;
        y[i] = prof > 0.0 ? x[i] / prof : 0.0;
    }
    return y;
}

static std::vector<double> abs_of(const std::vector<double>& x) {
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = std::abs(x[i]);
    return y;
}

// |r| divided by its intraday profile, and r rescaled by the same factor.
struct Standardized { std::vector<double> r, absr; bool seasonal_adj = false; };
static Standardized standardize(const Aggregated& a) {
    Standardized s;
    s.seasonal_adj = n_days(a) >= kMinDaysForProfile;
    const auto absr = abs_of(a.ret);
    s.absr = deseasonalize(absr, a.slot, s.seasonal_adj);
    s.r.resize(a.ret.size());
    for (size_t i = 0; i < a.ret.size(); ++i)
        s.r[i] = absr[i] > 0.0 ? a.ret[i] * (s.absr[i] / absr[i]) : 0.0;
    return s;
}

static std::string fmt(const char* f, double v) {
    char b[64]; std::snprintf(b, sizeof b, f, v); return b;
}

static std::string scale_name(int h) {
    if (h % 60 == 0) return std::to_string(h / 60) + " min";
    return std::to_string(h) + " s";
}

// ── Facts ──────────────────────────────────────────────────────────────────

static FactResult make_fact(std::string id, std::string name, std::string scale) {
    FactResult f;
    f.id = std::move(id); f.name = std::move(name); f.scale = std::move(scale);
    return f;
}

FactResult fat_tails(const Aggregated& r5) {
    FactResult f = make_fact("v2.1", "Caudas gordas", scale_name(r5.horizon_ticks));
    f.criterion = "curtose em excesso > 1 e IC 95% exclui 0";
    f.n = r5.ret.size();
    if (f.n < 100) { f.note = "amostra < 100"; return f; }
    f.value = excess_kurtosis(r5.ret);
    const auto ci = block_bootstrap(r5.ret, [](const std::vector<double>& x) { return excess_kurtosis(x); });
    f.ci_lo = ci.lo; f.ci_hi = ci.hi;
    if (f.value <= 1.0)      f.status = Status::Fail;
    else if (ci.lo > 0.0)    f.status = Status::Pass;
    else                     f.status = Status::Inconclusive;
    return f;
}

FactResult aggregational_gaussianity(const Aggregated& r1, const Aggregated& r5,
                                     const Aggregated& r30) {
    FactResult f = make_fact("v2.2", "Gaussianidade agregacional",
                 scale_name(r1.horizon_ticks) + " → " + scale_name(r30.horizon_ticks));
    f.criterion = "curtose decresce: k(1) > k(5) > k(30)";
    f.n = r30.ret.size();
    if (r30.ret.size() < 100) { f.note = "amostra de 30 min < 100"; return f; }
    const double k1 = excess_kurtosis(r1.ret), k5 = excess_kurtosis(r5.ret),
                 k30 = excess_kurtosis(r30.ret);
    f.value = k1 - k30;
    f.ci_lo = f.ci_hi = f.value;
    f.note = "k = " + fmt("%.2f", k1) + " / " + fmt("%.2f", k5) + " / " + fmt("%.2f", k30);
    f.status = (k1 > k5 && k5 > k30) ? Status::Pass : Status::Fail;
    return f;
}

FactResult no_autocorrelation(const Aggregated& r1, const Aggregated& r5) {
    FactResult f = make_fact("v2.3", "Ausência de autocorrelação",
                 scale_name(r1.horizon_ticks) + " e " + scale_name(r5.horizon_ticks));
    // Ten tests (two scales × five lags): Bonferroni at 5% family-wise, with
    // the robust SE — under volatility clustering 1/√N is too narrow and
    // rejects a martingale.
    const double z = 2.807;
    f.criterion = "|ACF(r, 1..5)| < 2,81·SE robusto (Bonferroni 5%)";
    f.n = r5.ret.size();
    if (r5.ret.size() < 100) { f.note = "amostra < 100"; return f; }
    double worst = 0.0, worst_rho = 0.0, worst_band = 0.0;
    std::string where;
    for (const Aggregated* a : {&r1, &r5}) {
        const std::vector<int> lags{1, 2, 3, 4, 5};
        const auto rho = acf_at(a->ret, lags);
        for (size_t j = 0; j < lags.size(); ++j) {
            const double se = acf_robust_se(a->ret, lags[j]);
            const double zz = std::abs(rho[j]) / se;
            if (zz > worst) {
                worst = zz; worst_rho = rho[j]; worst_band = z * se;
                where = scale_name(a->horizon_ticks) + " lag " + std::to_string(lags[j]);
            }
        }
    }
    f.value = worst_rho;
    f.ci_lo = -worst_band; f.ci_hi = worst_band;
    f.note  = "pior: " + where + ", |z| = " + fmt("%.2f", worst);
    f.status = worst < z ? Status::Pass : Status::Fail;
    return f;
}

FactResult vol_clustering(const Aggregated& r5) {
    FactResult f = make_fact("v2.4", "Clustering de volatilidade", scale_name(r5.horizon_ticks));
    f.criterion = "ACF(|r|, 1) > 2/√N (|r| dessazonalizado)";
    f.n = r5.ret.size();
    if (f.n < 100) { f.note = "amostra < 100"; return f; }
    const auto s = standardize(r5);
    f.value = acf_at(s.absr, {1})[0];
    const double band = 2.0 / std::sqrt(double(f.n));
    f.ci_lo = -band; f.ci_hi = band;
    f.note = std::string(s.seasonal_adj ? "" : "sem dessazonalização (< 5 sessões); ")
           + "bruto " + fmt("%.3f", acf_at(abs_of(r5.ret), {1})[0]);
    f.status = f.value > band ? Status::Pass : Status::Fail;
    return f;
}

FactResult vol_long_memory(const Aggregated& r1, int max_lag) {
    FactResult f = make_fact("v2.5", "Memória longa da volatilidade",
                 scale_name(r1.horizon_ticks) + ", lags 1–" + std::to_string(max_lag));
    f.criterion = "ACF(|r|) ∝ k^−β, β ∈ (0,05; 0,6), lei de potência vence exponencial (AIC)";
    f.n = r1.ret.size();
    if (f.n < static_cast<size_t>(4 * max_lag)) { f.note = "amostra < 4×lag máximo"; return f; }
    const auto s = standardize(r1);
    std::vector<int> lags(static_cast<size_t>(max_lag));
    std::iota(lags.begin(), lags.end(), 1);
    const auto rho = acf_at(s.absr, lags);
    const double band = 2.0 / std::sqrt(double(f.n));
    const DecayFit fit = fit_decay(lags, rho);
    f.value = fit.beta;
    f.note = "ACF(1) = " + fmt("%.3f", rho[0]) + ", bins " + std::to_string(fit.bins)
           + ", AIC pot/exp " + fmt("%.1f", fit.aic_power) + "/" + fmt("%.1f", fit.aic_exp);
    if (!(rho[0] > band) || !fit.valid) { f.status = Status::Fail; return f; }
    // No interval: a block bootstrap needs blocks much longer than the 390
    // lags fitted, which leaves too few blocks in 60 sessions.  Point estimate.
    f.ci_lo = f.ci_hi = fit.beta;
    f.status = (fit.power_beats_exp() && fit.beta > 0.05 && fit.beta < 0.6)
             ? Status::Pass : Status::Fail;
    return f;
}

FactResult gain_loss_asymmetry(const Aggregated& r30) {
    FactResult f = make_fact("v2.6", "Assimetria ganho-perda", scale_name(r30.horizon_ticks));
    f.criterion = "skew < 0 com IC 95% excluindo 0";
    f.n = r30.ret.size();
    if (f.n < 100) { f.note = "amostra < 100"; return f; }
    f.value = skewness(r30.ret);
    const auto ci = block_bootstrap(r30.ret, [](const std::vector<double>& x) { return skewness(x); });
    f.ci_lo = ci.lo; f.ci_hi = ci.hi;
    if (ci.hi < 0.0)      f.status = Status::Pass;
    else if (ci.lo > 0.0) f.status = Status::Fail;   // significantly the wrong sign
    else                  f.status = Status::Inconclusive;
    return f;
}

FactResult tail_index(const Aggregated& r5) {
    FactResult f = make_fact("v2.7", "Índice de cauda", scale_name(r5.horizon_ticks));
    f.criterion = "Hill médio sobre k = 2,5% / 5% / 10% de N, α ∈ [2; 5]";
    f.n = r5.ret.size();
    if (f.n < 200) { f.note = "amostra < 200"; return f; }
    double lo = 1e9, hi = -1e9, sum = 0.0; int c = 0;
    for (double frac : {0.025, 0.05, 0.10}) {
        const int k = std::max(10, static_cast<int>(frac * double(f.n)));
        const HillResult h = HillEstimator::estimate_abs(r5.ret, k);
        if (!h.valid) continue;
        lo = std::min(lo, h.alpha); hi = std::max(hi, h.alpha); sum += h.alpha; ++c;
    }
    if (c == 0) { f.note = "Hill inválido"; return f; }
    f.value = sum / c; f.ci_lo = lo; f.ci_hi = hi;
    f.note = "intervalo = faixa entre os k";
    f.status = (f.value >= 2.0 && f.value <= 5.0) ? Status::Pass : Status::Fail;
    return f;
}

static double leverage_stat(const std::vector<double>& r) {
    const auto a = abs_of(r);
    double s = 0.0;
    for (int k = 1; k <= 10; ++k) {
        if (r.size() <= static_cast<size_t>(k) + 3) return 0.0;
        std::vector<double> x(r.begin(), r.end() - k), y(a.begin() + k, a.end());
        s += pearson(x, y);
    }
    return s / 10.0;
}

FactResult leverage(const Aggregated& r5) {
    FactResult f = make_fact("v2.8", "Efeito alavancagem", scale_name(r5.horizon_ticks));
    f.criterion = "média de corr(r_t, |r_{t+k}|), k = 1..10, com IC 95% < 0";
    f.n = r5.ret.size();
    if (f.n < 200) { f.note = "amostra < 200"; return f; }
    const auto s = standardize(r5);
    f.value = leverage_stat(s.r);
    // Blocks long enough to keep most (t, t+k) pairs inside a block.
    const auto ci = block_bootstrap(s.r, leverage_stat, 400, 50);
    f.ci_lo = ci.lo; f.ci_hi = ci.hi;
    f.status = ci.hi < 0.0 ? Status::Pass : Status::Fail;
    return f;
}

FactResult vol_volume(const Aggregated& r5) {
    FactResult f = make_fact("v2.9", "Correlação volatilidade-volume", scale_name(r5.horizon_ticks));
    f.criterion = "corr(|r|, volume) > 2/√N (ambos dessazonalizados)";
    f.n = r5.ret.size();
    if (f.n < 100) { f.note = "amostra < 100"; return f; }
    const auto s = standardize(r5);
    const auto v = deseasonalize(r5.volume, r5.slot, s.seasonal_adj);
    f.value = pearson(s.absr, v);
    const double band = 2.0 / std::sqrt(double(f.n));
    f.ci_lo = -band; f.ci_hi = band;
    f.note = "bruto " + fmt("%.3f", pearson(abs_of(r5.ret), r5.volume));
    f.status = f.value > band ? Status::Pass : Status::Fail;
    return f;
}

FactResult trade_sign_memory(const std::vector<double>& signs, int max_lag) {
    FactResult f = make_fact("v2.10", "Memória do sinal de trade", "por trade");
    f.criterion = "ACF do sinal ∝ k^−γ, γ ∈ (0,2; 0,9), lei de potência vence exponencial (AIC)";
    f.n = signs.size();
    if (f.n < static_cast<size_t>(20 * max_lag)) { f.note = "amostra < 20×lag máximo"; return f; }
    const auto lags = geometric_lags(max_lag);
    const auto rho  = acf_at(signs, lags);
    const DecayFit fit = fit_decay(lags, rho);
    const double band = 2.0 / std::sqrt(double(f.n));
    f.value = fit.beta;
    f.ci_lo = f.ci_hi = fit.beta;
    f.note = "ACF(1) = " + fmt("%.3f", rho[0]) + ", bins " + std::to_string(fit.bins)
           + ", AIC pot/exp " + fmt("%.1f", fit.aic_power) + "/" + fmt("%.1f", fit.aic_exp);
    if (!(rho[0] > band) || !fit.valid) { f.status = Status::Fail; return f; }
    f.status = (fit.power_beats_exp() && fit.beta > 0.2 && fit.beta < 0.9)
             ? Status::Pass : Status::Fail;
    return f;
}

// ── Battery ────────────────────────────────────────────────────────────────

Battery compute(const LogData& d, const TimeScale& ts) {
    const int m1  = static_cast<int>(std::lround(60.0   / ts.seconds_per_tick));
    const int m5  = static_cast<int>(std::lround(300.0  / ts.seconds_per_tick));
    const int m30 = static_cast<int>(std::lround(1800.0 / ts.seconds_per_tick));
    const auto r1 = aggregate(d, m1, ts), r5 = aggregate(d, m5, ts), r30 = aggregate(d, m30, ts);
    const int max_lag = std::max(10, static_cast<int>(std::lround(ts.ticks_per_day() / m1)));

    Battery b;
    b.facts = {
        fat_tails(r5),
        aggregational_gaussianity(r1, r5, r30),
        no_autocorrelation(r1, r5),
        vol_clustering(r5),
        vol_long_memory(r1, max_lag),
        gain_loss_asymmetry(r30),
        tail_index(r5),
        leverage(r5),
        vol_volume(r5),
        trade_sign_memory(d.trade_signs),
    };
    for (const auto& f : b.facts) {
        if (f.status == Status::Pass)      ++b.pass;
        else if (f.status == Status::Fail) ++b.fail;
        else                               ++b.inconclusive;
    }
    return b;
}

// A '|' inside a cell (|r|, |ACF|) would split the markdown row.
static std::string cell(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '|') o += '\\'; o += c; }
    return o;
}

std::string Battery::to_text() const {
    std::ostringstream o;
    o << "Bateria v2 (retornos agregados no tempo)\n"
      << "========================================\n"
      << "| # | Fato | Escala | Valor | IC 95% | N | Estado | Critério |\n"
      << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& f : facts) {
        o << "| " << f.id << " | " << f.name << " | " << f.scale << " | "
          << fmt("%.4f", f.value) << " | ["
          << fmt("%.4f", f.ci_lo) << "; " << fmt("%.4f", f.ci_hi) << "] | "
          << f.n << " | " << to_string(f.status) << " | " << cell(f.criterion);
        if (!f.note.empty()) o << " — " << cell(f.note);
        o << " |\n";
    }
    o << "\nResultado v2: " << pass << " PASSA, " << fail << " FALHA, "
      << inconclusive << " INCONCLUSIVO (de " << facts.size() << ")\n";
    return o.str();
}

}  // namespace sfv2
