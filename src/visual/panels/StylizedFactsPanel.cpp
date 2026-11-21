#include "StylizedFactsPanel.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cmath>
#include <numeric>

// Peter Acklam's rational approximation to Φ⁻¹(p).
double StylizedFactsPanel::inv_normal_cdf(double p) {
    static const double a[] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00 };
    static const double b[] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01 };
    static const double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00 };
    static const double d[] = {
         7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00 };

    if (p <= 0.0) return -1e10;
    if (p >= 1.0) return  1e10;
    if (p < 0.02425) {
        double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p > 0.97575) {
        double q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    double q = p - 0.5, r = q * q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
}

void StylizedFactsPanel::ingest_return(const MarketSnapshot& snap) {
    if (snap.mid_price <= 0) return;
    double log_p = std::log(static_cast<double>(snap.mid_price));
    if (prev_log_price_ != 0.0) {
        double r = log_p - prev_log_price_;
        returns_.push_back(r);
        if (static_cast<int>(returns_.size()) > kMaxReturns)
            returns_.erase(returns_.begin());
    }
    prev_log_price_ = log_p;
}

void StylizedFactsPanel::recompute_stats() {
    int n = static_cast<int>(returns_.size());
    if (n < kMaxLag + 10) return;

    // Delegate ACF computation to shared AcfComputer.
    acf_ret_ = AcfComputer::compute(returns_, kMaxLag);
    acf_abs_ = AcfComputer::compute_abs(returns_, kMaxLag);
    hill_    = HillEstimator::estimate_abs(returns_);

    // Build lag axis.
    lags_.resize(kMaxLag + 1);
    for (int lag = 0; lag <= kMaxLag; ++lag) lags_[lag] = static_cast<double>(lag);

    // Q-Q plot vs normal.
    std::vector<double> sorted = returns_;
    std::sort(sorted.begin(), sorted.end());
    qq_x_.resize(n);
    qq_y_.resize(n);
    double std_dev = (acf_ret_.variance > 0) ? std::sqrt(acf_ret_.variance) : 1.0;
    for (int i = 0; i < n; ++i) {
        double p = (i + 0.5) / n;
        qq_x_[i] = inv_normal_cdf(p) * std_dev + acf_ret_.mean;
        qq_y_[i] = sorted[i];
    }
}

// ── Live pass/fail badges ─────────────────────────────────────────────────────

static void badge(const char* label, bool pass) {
    ImVec4 col = pass ? ImVec4(0.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.9f, 0.2f, 0.1f, 1.0f);
    ImGui::TextColored(col, "%s %s", pass ? "PASS" : "FAIL", label);
}

void StylizedFactsPanel::draw(const MarketSnapshot& snap) {
    ingest_return(snap);
    recompute_stats();

    int n = static_cast<int>(returns_.size());
    ImGui::Text("Return history: %d samples", n);
    if (n < 30) { ImGui::TextDisabled("Building history..."); return; }

    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y;

    // ── Live badges for 8 stylized facts ─────────────────────────────────────
    {
        // 1: Fat tails
        double mean = acf_ret_.mean;
        double var  = acf_ret_.variance;
        double m4 = 0.0;
        for (double r : returns_) { double d = r - mean; m4 += d*d*d*d; }
        double kurt = (var > 0 && n > 0) ? (m4 / n) / (var * var) - 3.0 : 0.0;
        badge("1: Fat tails (kurt>1)", kurt > 1.0);
        ImGui::SameLine();

        // 2: Return ACF ≈ 0
        bool acf0 = !acf_ret_.acf.empty() && acf_ret_.acf.size() > 1 &&
                    std::abs(acf_ret_.acf[1]) < acf_ret_.confidence_band();
        badge("2: RetACF≈0", acf0);
        ImGui::SameLine();

        // 3: Vol clustering
        bool vol_clust = acf_abs_.acf.size() > 1 && acf_abs_.acf[1] > 0.05;
        badge("3: VolCluster", vol_clust);
        ImGui::SameLine();

        // 4: Long memory
        double mean_acf_abs = 0.0;
        int cnt = 0;
        for (int lag = 1; lag <= 10 && lag < (int)acf_abs_.acf.size(); ++lag) {
            mean_acf_abs += acf_abs_.acf[lag]; ++cnt;
        }
        badge("4: LongMem", cnt > 0 && mean_acf_abs / cnt > 0.02);
    }

    {
        // 5: Skewness
        double m3 = 0.0;
        double mean = acf_ret_.mean, var = acf_ret_.variance;
        for (double r : returns_) { double d = r - mean; m3 += d*d*d; }
        double skew = (var > 0 && n > 0) ? (m3 / n) / std::pow(var, 1.5) : 0.0;
        badge("5: Skew<0", skew < 0.0);
        ImGui::SameLine();

        // 6: Hill α
        badge("6: Hill α∈[1.8,6]",
              hill_.valid && hill_.alpha >= 1.8 && hill_.alpha <= 6.0);
        ImGui::SameLine();

        // 7 & 8 are snapshot-level — show with last known values via panel state.
        // Spread CoV and OFI ACF are computed over the running returns buffer.
        ImGui::TextDisabled("7-8: see offline report");
    }

    // ── Main plots (2×2 grid) ─────────────────────────────────────────────────
    float plot_h = (h - 60.0f) * 0.5f;

    if (ImPlot::BeginPlot("Return Histogram##sf", ImVec2(w * 0.5f, plot_h))) {
        ImPlot::SetupAxes("Log-Return", "Count",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotHistogram("Returns", returns_.data(), n,
                              ImPlotBin_Sturges, 1.0, ImPlotRange());
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (!qq_x_.empty()) {
        if (ImPlot::BeginPlot("Q-Q vs Normal##sf", ImVec2(w * 0.5f, plot_h))) {
            ImPlot::SetupAxes("Theoretical", "Empirical",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotScatter("Q-Q", qq_x_.data(), qq_y_.data(),
                                static_cast<int>(qq_x_.size()));
            double lo = qq_x_.front(), hi = qq_x_.back();
            double ref_x[2] = {lo, hi}, ref_y[2] = {lo, hi};
            ImPlot::PlotLine("45°", ref_x, ref_y, 2);
            ImPlot::EndPlot();
        }
    }

    if (!acf_ret_.acf.empty()) {
        if (ImPlot::BeginPlot("ACF Returns##sf", ImVec2(w * 0.5f, -1))) {
            ImPlot::SetupAxes("Lag", "ACF",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("ACF(r)", lags_.data(), acf_ret_.acf.data(),
                             static_cast<int>(lags_.size()), 0.67);
            ImPlot::EndPlot();
        }
        ImGui::SameLine();
        if (ImPlot::BeginPlot("ACF |Returns|##sf", ImVec2(w * 0.5f, -1))) {
            ImPlot::SetupAxes("Lag", "ACF",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("ACF(|r|)", lags_.data(), acf_abs_.acf.data(),
                             static_cast<int>(lags_.size()), 0.67);
            if (hill_.valid) {
                ImGui::SameLine();
                ImGui::Text("Hill α = %.2f", hill_.alpha);
            }
            ImPlot::EndPlot();
        }
    }
}
