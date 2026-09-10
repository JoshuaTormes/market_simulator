#pragma once
// Stylized-facts report: runs 8 empirical tests against a binary simulation log
// and emits a human-readable table + optional CSV.
//
// The 8 facts tested (from Cont 2001 and related literature):
//   1. Fat tails          — excess kurtosis of log-returns > 1.0
//   2. Return ACF ≈ 0     — |ACF(r, lag=1)| and |ACF(r, lag=2)| < 0.05
//   3. Vol clustering     — ACF(|r|, lag=1) > 0.05
//   4. Long memory vol    — mean ACF(|r|, lag 1..10) > 0.02
//   5. Gain-loss asymm.   — skewness(r) < 0  (crash regimes skew left)
//   6. Hill tail index    — α̂ ∈ [1.8, 6.0]  (power-law tails)
//   7. Spread variation   — CoV(spread) > 0.08
//   8. OFI clustering     — ACF(OFI, lag=1) > 0.03
#include "core/Types.h"
#include <string>
#include <vector>

struct StyleResult {
    std::string name;
    bool        passed    = false;
    double      value     = 0.0;
    double      threshold = 0.0;
    double      ci_band   = 0.0;  // 95% confidence band (where applicable, e.g. ACF)
    std::string description;
};

// Price-discovery diagnostics.  These are not stylized facts — they answer the
// prior question of whether the traded price tracks the latent fundamental at
// all.  Requires a v3 log (fundamental_value present); with a v2 log every
// fundamental-dependent field is left at its sentinel and `has_fundamental` is
// false.
struct PriceDiscovery {
    bool   has_fundamental = false;
    // corr(log-return of V, log-return of mid) over non-overlapping horizons.
    double corr_h1   = 0.0;
    double corr_h5   = 0.0;
    double corr_h20  = 0.0;
    double corr_h100 = 0.0;
    // Pricing error g_t = log(mid_t / V_t).
    double gap_std       = 0.0;  // std(g) — dimensionless, ~fraction of price
    double gap_mean      = 0.0;
    double gap_half_life = 0.0;  // ticks for an AR(1) shock in g to halve (<=0: no mean reversion)
    // Book health.
    double two_sided_frac  = 0.0;  // fraction of snapshots with a positive spread
    double zero_ret_frac   = 0.0;  // fraction of snapshot-to-snapshot returns exactly 0
    double mm_self_trade_frac = 0.0;  // share of volume where maker and taker are both MMs
};

struct AnalysisReport {
    std::vector<StyleResult> results;
    int    passed_count  = 0;
    int    total_count   = 0;
    size_t n_returns     = 0;   // number of log-return observations
    size_t n_trades      = 0;   // total trades in log
    size_t n_snapshots   = 0;   // total snapshots in log
    int    flash_crashes = 0;   // detected flash-crash events (informational)
    PriceDiscovery pd;          // price-discovery diagnostics

    // Extra diagnostics (not counted in pass/fail, reported separately)
    double ljung_box_q     = 0.0; // Ljung-Box Q(10) for returns (H0: white noise)
    double ljung_box_p     = 1.0; // p-value of Q under chi^2(df); small p → reject white noise
    int    ljung_box_df    = 10;  // degrees of freedom
    double trade_sign_acf1 = 0.0; // ACF(trade_sign, lag=1) — order splitting indicator
    double vol_vol_corr    = 0.0; // Pearson corr(|return|, trade_volume_per_period)
    double hill_alpha_mean = 0.0; // Mean Hill α across multiple k values
    double hill_alpha_std  = 0.0; // Std of Hill α (stability measure)

    // Markdown-formatted table string.
    std::string to_text() const;

    // Write CSV rows to path (overwrites).
    void to_csv(const std::string& path) const;
};

class Report {
public:
    // `mm_ids` lists the market-maker agent ids so maker/taker pairs that are
    // both market makers can be measured (MM-vs-MM churn is not real trading).
    // Pass an empty vector when the ids are unknown; the metric then reads 0.
    static AnalysisReport run(const std::string& bin_path,
                              const std::vector<AgentId>& mm_ids = {});
};
