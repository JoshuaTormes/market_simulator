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

    // ── ACF ──────────────────────────────────────────────────────────────────
    double mean = std::accumulate(returns_.begin(), returns_.end(), 0.0) / n;
    double var  = 0.0;
    for (double r : returns_) var += (r - mean) * (r - mean);

    acf_ret_.resize(kMaxLag + 1);
    acf_abs_.resize(kMaxLag + 1);
    lags_.resize(kMaxLag + 1);
    for (int lag = 0; lag <= kMaxLag; ++lag) {
        lags_[lag] = static_cast<double>(lag);
        double cov_r = 0.0, cov_a = 0.0;
        int    cnt   = n - lag;
        for (int i = 0; i < cnt; ++i) {
            cov_r += (returns_[i] - mean) * (returns_[i + lag] - mean);
            cov_a += (std::abs(returns_[i]) - mean) *
                     (std::abs(returns_[i + lag]) - mean);
        }
        acf_ret_[lag] = (var > 0) ? cov_r / var : 0.0;
        acf_abs_[lag] = (var > 0) ? cov_a / var : 0.0;
    }

    // ── Q-Q plot ──────────────────────────────────────────────────────────────
    std::vector<double> sorted = returns_;
    std::sort(sorted.begin(), sorted.end());
    qq_x_.resize(n);
    qq_y_.resize(n);
    // Compute sample std dev for scaling
    double std_dev = (var > 0) ? std::sqrt(var / n) : 1.0;
    for (int i = 0; i < n; ++i) {
        double p = (i + 0.5) / n;
        qq_x_[i] = inv_normal_cdf(p) * std_dev + mean; // theoretical
        qq_y_[i] = sorted[i];                           // empirical
    }
}

void StylizedFactsPanel::draw(const MarketSnapshot& snap) {
    ingest_return(snap);
    recompute_stats();

    int n = static_cast<int>(returns_.size());
    ImGui::Text("Return history: %d samples", n);
    if (n < 30) { ImGui::TextDisabled("Building history..."); return; }

    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y;

    // ── Top row: histogram + Q-Q ─────────────────────────────────────────────
    if (ImPlot::BeginPlot("Return Histogram##sf", ImVec2(w * 0.5f, h * 0.5f))) {
        ImPlot::SetupAxes("Log-Return", "Count",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotHistogram("Returns", returns_.data(), n,
                              ImPlotBin_Sturges, 1.0, ImPlotRange());
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (!qq_x_.empty()) {
        if (ImPlot::BeginPlot("Q-Q vs Normal##sf", ImVec2(w * 0.5f, h * 0.5f))) {
            ImPlot::SetupAxes("Theoretical", "Empirical",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotScatter("Q-Q", qq_x_.data(), qq_y_.data(),
                                static_cast<int>(qq_x_.size()));
            // 45° reference line
            double lo = qq_x_.front(), hi = qq_x_.back();
            double ref_x[2] = {lo, hi}, ref_y[2] = {lo, hi};
            ImPlot::PlotLine("45°", ref_x, ref_y, 2);
            ImPlot::EndPlot();
        }
    }

    // ── Bottom row: ACF returns + ACF |returns| ──────────────────────────────
    if (!acf_ret_.empty()) {
        if (ImPlot::BeginPlot("ACF Returns##sf", ImVec2(w * 0.5f, -1))) {
            ImPlot::SetupAxes("Lag", "ACF",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("ACF(r)", lags_.data(), acf_ret_.data(),
                             static_cast<int>(lags_.size()), 0.67);
            ImPlot::EndPlot();
        }
        ImGui::SameLine();
        if (ImPlot::BeginPlot("ACF |Returns|##sf", ImVec2(w * 0.5f, -1))) {
            ImPlot::SetupAxes("Lag", "ACF",
                              ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("ACF(|r|)", lags_.data(), acf_abs_.data(),
                             static_cast<int>(lags_.size()), 0.67);
            ImPlot::EndPlot();
        }
    }
}
