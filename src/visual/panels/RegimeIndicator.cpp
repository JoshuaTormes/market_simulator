#include "RegimeIndicator.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>

static const char* kRegimeNames[] = { "Low Vol", "High Vol", "Crash", "N/A" };
static const ImVec4 kRegimeColors[] = {
    ImVec4(0.20f, 0.80f, 0.20f, 1.0f),   // 0: green
    ImVec4(1.00f, 0.60f, 0.00f, 1.0f),   // 1: orange
    ImVec4(0.90f, 0.10f, 0.10f, 1.0f),   // 2: red
    ImVec4(0.50f, 0.50f, 0.50f, 1.0f),   // N/A: grey
};

static int regime_idx(int regime) {
    if (regime < 0 || regime > 2) return 3;
    return regime;
}

void RegimeIndicator::draw(int regime, double fundamental_value, double tick) {
    t_hist_.push_back(tick);
    regime_hist_.push_back(static_cast<double>(regime));
    fund_hist_.push_back(fundamental_value);
    while (static_cast<int>(t_hist_.size()) > kMaxHistory) {
        t_hist_.erase(t_hist_.begin());
        regime_hist_.erase(regime_hist_.begin());
        fund_hist_.erase(fund_hist_.begin());
    }

    int idx = regime_idx(regime);
    const ImVec4& col = kRegimeColors[idx];

    ImGui::TextColored(col, "Current Regime: %s (%d)", kRegimeNames[idx], regime);
    ImGui::Text("Fundamental Value: %.4f", fundamental_value);

    // Colour bar
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      pos  = ImGui::GetCursorScreenPos();
    float       bw   = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(pos, ImVec2(pos.x + bw, pos.y + 18.0f),
                      ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(bw, 20.0f));

    ImGui::Separator();
    int n = static_cast<int>(t_hist_.size());
    if (n < 2) return;

    float h2 = ImGui::GetContentRegionAvail().y * 0.5f;

    if (ImPlot::BeginPlot("Regime##ri", ImVec2(-1, h2))) {
        ImPlot::SetupAxes("Tick", "Regime ID",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Regime", t_hist_.data(), regime_hist_.data(), n);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Fundamental##ri", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Tick", "Value",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Fundamental", t_hist_.data(), fund_hist_.data(), n);
        ImPlot::EndPlot();
    }
}
