#pragma once
// Stylized-facts report: runs 8 empirical tests against a binary simulation log
// and emits a human-readable table + optional CSV.
//
// The 8 facts tested (from Cont 2001 and related literature):
//   1. Fat tails          — excess kurtosis of log-returns > 1.0
//   2. Return ACF ≈ 0     — |ACF(r, lag=1)| within 95% confidence band
//   3. Vol clustering     — ACF(|r|, lag=1) > 0.05
//   4. Long memory vol    — mean ACF(|r|, lag 1..10) > 0.02
//   5. Gain-loss asymm.   — skewness(r) < 0  (crash regimes skew left)
//   6. Hill tail index    — α̂ ∈ [1.8, 6.0]  (power-law tails)
//   7. Spread variation   — CoV(spread) > 0.08
//   8. OFI clustering     — ACF(OFI, lag=1) > 0.03
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

struct AnalysisReport {
    std::vector<StyleResult> results;
    int    passed_count  = 0;
    int    total_count   = 0;
    size_t n_returns     = 0;   // number of log-return observations
    size_t n_trades      = 0;   // total trades in log
    size_t n_snapshots   = 0;   // total snapshots in log
    int    flash_crashes = 0;   // detected flash-crash events (informational)

    // Extra diagnostics (not counted in pass/fail, reported separately)
    double ljung_box_q     = 0.0; // Ljung-Box Q(10) for returns (H0: white noise)
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
    static AnalysisReport run(const std::string& bin_path);
};
