#include "MicrostructureDashboard.h"
#include "imgui.h"
#include "implot.h"

static void trim(std::vector<double>& v, int max_size) {
    if (static_cast<int>(v.size()) > max_size)
        v.erase(v.begin(), v.begin() + (static_cast<int>(v.size()) - max_size));
}

void MicrostructureDashboard::push(const MarketSnapshot& snap) {
    t_hist_.push_back(static_cast<double>(snap.tick));
    spread_hist_.push_back(static_cast<double>(snap.spread));
    imb_l1_hist_.push_back(snap.book_imbalance[0]);
    imb_l5_hist_.push_back(snap.book_imbalance[4]);
    ofi_hist_.push_back(snap.order_flow_imbalance);
    vol_s_hist_.push_back(snap.realized_vol[0]);
    vol_m_hist_.push_back(snap.realized_vol[1]);
    vol_l_hist_.push_back(snap.realized_vol[2]);

    trim(t_hist_,      kMaxHistory);
    trim(spread_hist_, kMaxHistory);
    trim(imb_l1_hist_, kMaxHistory);
    trim(imb_l5_hist_, kMaxHistory);
    trim(ofi_hist_,    kMaxHistory);
    trim(vol_s_hist_,  kMaxHistory);
    trim(vol_m_hist_,  kMaxHistory);
    trim(vol_l_hist_,  kMaxHistory);
}

void MicrostructureDashboard::draw(const MarketSnapshot& snap) {
    push(snap);
    int n = static_cast<int>(t_hist_.size());
    if (n < 2) { ImGui::TextDisabled("Building history..."); return; }

    const double* xs = t_hist_.data();

    // ── Row 1: Spread | Book Imbalance ─────────────────────────────────────
    float half_w = ImGui::GetContentRegionAvail().x * 0.5f;
    float half_h = ImGui::GetContentRegionAvail().y * 0.5f;

    if (ImPlot::BeginPlot("Spread##ms", ImVec2(half_w, half_h))) {
        ImPlot::SetupAxes("Tick", "Spread (ticks)",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Spread", xs, spread_hist_.data(), n);
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (ImPlot::BeginPlot("Imbalance##ms", ImVec2(half_w, half_h))) {
        ImPlot::SetupAxes("Tick", "Imbalance",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("L1", xs, imb_l1_hist_.data(), n);
        ImPlot::PlotLine("L5", xs, imb_l5_hist_.data(), n);
        ImPlot::EndPlot();
    }

    // ── Row 2: OFI | Realized Vol ──────────────────────────────────────────
    if (ImPlot::BeginPlot("OFI##ms", ImVec2(half_w, -1))) {
        ImPlot::SetupAxes("Tick", "OFI",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("OFI", xs, ofi_hist_.data(), n);
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    if (ImPlot::BeginPlot("Realized Vol##ms", ImVec2(half_w, -1))) {
        ImPlot::SetupAxes("Tick", "Realized Vol",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Short",  xs, vol_s_hist_.data(), n);
        ImPlot::PlotLine("Medium", xs, vol_m_hist_.data(), n);
        ImPlot::PlotLine("Long",   xs, vol_l_hist_.data(), n);
        ImPlot::EndPlot();
    }
}
