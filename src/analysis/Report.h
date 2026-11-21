#pragma once
// Stylized-facts report: runs 8 empirical tests against a binary simulation log
// and emits a human-readable table + optional CSV.
//
// The 8 facts tested (from Cont 2001 and related literature):
//   1. Fat tails          — excess kurtosis of log-returns > 1.0
//   2. Return ACF ≈ 0     — |ACF(r, lag=1)| and |ACF(r, lag=2)| < 0.05
//   3. Vol clustering     — ACF(|r|, lag=1) > 0.05
//   4. Long memory vol    — mean ACF(|r|, lag 1..10) > 0.02
//   5. Gain-loss asymm.   — skewness(r) < 0
//   6. Hill tail index    — α̂ ∈ [1.8, 6.0]  (power-law tails)
//   7. Spread variation   — CoV(spread) > 0.08
//   8. OFI clustering     — ACF(OFI, lag=1) > 0.03
#include "core/Types.h"
#include "core/Config.h"
#include "analysis/LogData.h"
#include "analysis/StylizedFactsV2.h"
#include <map>
#include <string>
#include <vector>

struct StyleResult {
    std::string name;
    bool        passed    = false;
    double      value     = 0.0;
    double      threshold = 0.0;
    double      ci_band   = 0.0;  // ±2/√N white-noise band (ACF facts only; legacy column)
    std::string description;
    // Sampling uncertainty of `value`.  The pass/fail rule above is untouched;
    // these say how far from the threshold the value sits, in standard errors.
    char        dir   = '>';  // '>' passes above threshold, '<' below, 'r' inside a range
    double      se    = 0.0;
    double      ci_lo = 0.0, ci_hi = 0.0;   // 95%
    double      z     = 0.0;  // (value − threshold)/se; for 'r', distance to the nearest bound
    std::string se_method;    // "robusta", "bootstrap", "assintótica"
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

    // v4 logs split log V into diffusion + Σ news.  corr(h) of r_mid against
    // each component, at h = 1, 5, 20, 100 ticks.
    bool   has_components = false;
    double corr_diff[4] = {0, 0, 0, 0};
    double corr_news[4] = {0, 0, 0, 0};
    // Tracking away from the news: every tick within ±60 s of a news event is
    // dropped (and every horizon block that touches one).
    int    n_news           = 0;
    double excluded_frac    = 0.0;
    double gap_std_ex_news  = 0.0;
    double corr_h20_ex_news = 0.0;
    double corr_h100_ex_news = 0.0;
};

// Impact of completed institutional parents (Bouchaud et al. 2018, ch. 12):
// I = sign · log(mid at end / mid before start), against Q/ADV.  The square-
// root law is I ≈ Y σ_D (Q/ADV)^δ with δ ≈ 0.5.  "Permanent" is the same
// return measured one parent-duration after the end.
struct MetaorderImpact {
    int    n = 0;                 // parents with executed quantity
    int    n_perm = 0;            // of those, with a full post-period inside the log
    double adv = 0.0;             // average daily volume (lots)
    double sigma_daily = 0.0;     // from 5-min returns
    double delta = 0.0, delta_lo = 0.0, delta_hi = 0.0;   // exponent and bootstrap 95% CI
    bool   delta_valid = false;
    double boot_valid_frac = 0.0; // bootstrap replicates where the fit was possible
    double y_prefactor = 0.0;     // Σ I / Σ σ_D·sqrt(Q/ADV)
    double temp_mean_bp = 0.0, perm_mean_bp = 0.0, perm_over_temp = 0.0;  // on the n_perm subset
    struct Bin { double q_adv = 0.0, impact_bp = 0.0; int count = 0; };
    std::vector<Bin> bins;
};

// Realism diagnostics: the quantities an economist checks before the stylized
// facts — is the volatility the one configured, is impact concave, does the
// session have a shape, does the maker population behave like several makers.
struct Realism {
    double ann_vol_mid_1m   = 0.0;  // annualized vol of mid, 1-min non-overlapping returns
    double ann_vol_fund_1m  = 0.0;  // same for the latent fundamental (0 without v3 log)
    double signature_ratio  = 0.0;  // annualized vol at 1 tick / at 1 min (>1: microstructure noise)
    double impact_exponent  = 0.0;  // slope of log E[sign(OFI)·Δmid] vs log|OFI| (0.5 = square root)
    double impact_ticks_50  = 0.0;  // mean next-tick impact of ~50-lot signed flow, in ticks
    int    impact_bins      = 0;    // bins that entered the regression
    double intraday_u_ratio = 0.0;  // volume/tick at open+close half-hours over mid-session (0: < 1 day)
    double mean_spread_bp   = 0.0;  // mean positive spread in basis points of the mid
    double mm_top_share     = 0.0;  // largest single maker's share of all maker volume
    double non_mm_top_depth_frac = -1.0; // non-maker share of touch depth (-1: not measured)
    // Emergent intraday shapes (nothing in the model imposes these directly).
    double intraday_u_absr   = 0.0;  // mean |r| 1-min at open+close half-hours over mid-session
    double intraday_u_spread = 0.0;  // same ratio for the spread (two-sided snapshots only)
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
    Realism        realism;     // realism diagnostics

    // Extra diagnostics (not counted in pass/fail, reported separately)
    double ljung_box_q     = 0.0; // Ljung-Box Q(10) for returns (H0: white noise)
    double ljung_box_p     = 1.0; // p-value of Q under chi^2(df); small p → reject white noise
    int    ljung_box_df    = 10;  // degrees of freedom
    double trade_sign_acf1 = 0.0; // ACF(trade_sign, lag=1) — order splitting indicator
    double vol_vol_corr    = 0.0; // Pearson corr(|return|, trade_volume_per_period)
    double hill_alpha_mean = 0.0; // Mean Hill α across multiple k values
    double hill_alpha_std  = 0.0; // Std of Hill α (stability measure)
    double ofi_acf1_signed = 0.0; // ACF(OFI, 1) with its sign (Fact 8 tests the absolute value)

    MetaorderImpact metaorder;
    sfv2::Battery   v2;           // time-aggregated battery (StylizedFactsV2)

    // Markdown-formatted table string.
    std::string to_text() const;

    // Write CSV rows to path (overwrites).
    void to_csv(const std::string& path) const;
};

class Report {
public:
    // Read a binary log into a LogData (see `run` for `mm_ids`).
    static LogData extract(const std::string& bin_path,
                           const std::vector<AgentId>& mm_ids = {});

    // Every statistic in the report, computed from data already in memory.
    static AnalysisReport compute(const LogData& d, const TimeScale& ts = TimeScale{});

    // `mm_ids` lists the market-maker agent ids so maker/taker pairs that are
    // both market makers can be measured (MM-vs-MM churn is not real trading).
    // Pass an empty vector when the ids are unknown; the metric then reads 0.
    // `ts` gives the tick length, needed to annualize volatility and to cut
    // the log into sessions for the intraday profile.
    static AnalysisReport run(const std::string& bin_path,
                              const std::vector<AgentId>& mm_ids = {},
                              const TimeScale& ts = TimeScale{});
};
