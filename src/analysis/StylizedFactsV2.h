#pragma once
// Stylized facts measured where the literature measures them: on returns
// aggregated in calendar time (1, 5, 30 minutes), inside a session, with an
// uncertainty attached to every number and a three-way verdict.
//
// The legacy battery (Report.cpp) tests 1-second snapshot returns, where the
// bid-ask bounce and the maker's requote cadence dominate.  Nothing here
// replaces it; this battery is reported beside it.
//
// Every statistic is a pure function of LogData + TimeScale.  Bootstrap draws
// use a fixed-seed engine local to the call, so a report is reproducible.
#include "analysis/LogData.h"
#include "core/Config.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sfv2 {

enum class Status { Pass, Fail, Inconclusive };
const char* to_string(Status s);

// ── Primitives ─────────────────────────────────────────────────────────────

struct Aggregated {
    int                 horizon_ticks = 0;
    std::vector<double> ret;      // log-returns between consecutive buckets of the same session
    std::vector<double> volume;   // volume traded in the later bucket of each return
    std::vector<int>    slot;     // bucket index within the session (intraday profile)
    std::vector<int64_t> day;     // session index
};

// Close of bucket j = last mid observed in [day_start + j·h, day_start + (j+1)·h).
// A return links two consecutive non-empty buckets of the same session; the
// overnight gap and empty buckets produce no return.
Aggregated aggregate(const LogData& d, int horizon_ticks, const TimeScale& ts);

double mean(const std::vector<double>& x);
double excess_kurtosis(const std::vector<double>& x);
double skewness(const std::vector<double>& x);
double pearson(const std::vector<double>& x, const std::vector<double>& y);

// Sample ACF (biased estimator, mean removed) at the requested lags.
std::vector<double> acf_at(const std::vector<double>& x, const std::vector<int>& lags);

// Heteroskedasticity-robust standard error of the lag-k sample autocorrelation
// (Taylor 1984 / Lo-MacKinlay): sqrt(Σ x_t² x_{t-k}²) / Σ x_t² on the demeaned
// series.  Equals 1/√N for i.i.d. data; wider under volatility clustering.
double acf_robust_se(const std::vector<double>& x, int lag);

// Moving-block bootstrap percentile interval of stat(x).
struct Interval { double lo = 0.0, hi = 0.0; };
template <class Stat>
Interval block_bootstrap(const std::vector<double>& x, Stat stat,
                         int n_boot = 400, int block = 0, double level = 0.95,
                         uint64_t seed = 0x5F2Bu);

// Decay fit of a positive ACF over [k_min, k_max]: the points are averaged in
// geometric bins (ratio 1.25) and the bins with a positive mean enter two
// least-squares fits in log space — power law ln ρ = a − β ln k, and
// exponential ln ρ = a − k/τ — compared by AIC (same parameter count).
struct DecayFit {
    bool   valid = false;   // fewer than 5 usable bins → false
    double beta  = 0.0;     // power-law exponent (ρ ∝ k^−β)
    double tau   = 0.0;     // exponential time scale
    double aic_power = 0.0, aic_exp = 0.0;
    int    bins  = 0;
    bool   power_beats_exp() const { return valid && aic_power < aic_exp; }
};
DecayFit fit_decay(const std::vector<int>& lags, const std::vector<double>& acf);

// Geometric lag grid 1..k_max, unique integers.
std::vector<int> geometric_lags(int k_max, double ratio = 1.15);

// ── The battery ────────────────────────────────────────────────────────────

struct FactResult {
    std::string id;          // "v2.1"
    std::string name;
    std::string scale;       // horizon the statistic is measured at
    Status      status = Status::Inconclusive;
    double      value  = 0.0;
    double      ci_lo  = 0.0, ci_hi = 0.0;   // 95% interval (or ± band) around value
    size_t      n      = 0;                  // observations behind the statistic
    std::string criterion;
    std::string note;        // free-form detail (secondary numbers)
};

struct Battery {
    std::vector<FactResult> facts;
    int pass = 0, fail = 0, inconclusive = 0;
    std::string to_text() const;
};

Battery compute(const LogData& d, const TimeScale& ts = TimeScale{});

// Individual facts, exposed for tests.
FactResult fat_tails(const Aggregated& r5);
FactResult aggregational_gaussianity(const Aggregated& r1, const Aggregated& r5,
                                     const Aggregated& r30);
FactResult no_autocorrelation(const Aggregated& r1, const Aggregated& r5);
FactResult vol_clustering(const Aggregated& r5);
FactResult vol_long_memory(const Aggregated& r1, int max_lag);
FactResult gain_loss_asymmetry(const Aggregated& r30);
FactResult tail_index(const Aggregated& r5);
FactResult leverage(const Aggregated& r5);
FactResult vol_volume(const Aggregated& r5);
FactResult trade_sign_memory(const std::vector<double>& signs, int max_lag = 1000);

}  // namespace sfv2

// ── Template implementation ────────────────────────────────────────────────
#include <algorithm>
#include <cmath>
#include <random>

template <class Stat>
sfv2::Interval sfv2::block_bootstrap(const std::vector<double>& x, Stat stat,
                                     int n_boot, int block, double level, uint64_t seed) {
    const size_t n = x.size();
    if (n < 10) return {};
    const size_t L = block > 0 ? static_cast<size_t>(block)
                               : std::max<size_t>(5, static_cast<size_t>(std::cbrt(double(n))));
    std::mt19937_64 eng(seed);
    std::uniform_int_distribution<size_t> pick(0, n - L);
    std::vector<double> s(n), out;
    out.reserve(static_cast<size_t>(n_boot));
    for (int b = 0; b < n_boot; ++b) {
        size_t i = 0;
        while (i < n) {
            const size_t start = pick(eng);
            for (size_t j = 0; j < L && i < n; ++j) s[i++] = x[start + j];
        }
        out.push_back(stat(s));
    }
    std::sort(out.begin(), out.end());
    const double a = (1.0 - level) / 2.0;
    const auto q = [&](double p) {
        const size_t k = std::min(out.size() - 1, static_cast<size_t>(p * double(out.size() - 1) + 0.5));
        return out[k];
    };
    return {q(a), q(1.0 - a)};
}
