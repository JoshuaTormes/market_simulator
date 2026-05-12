#include "OrderBookHeatmap.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cstring>

void OrderBookHeatmap::shift_and_fill(const MarketSnapshot& snap) {
    // Shift every row left by one column.
    for (int r = 0; r < kPriceRows; ++r) {
        float* row = &data_[r * kHistCols];
        std::memmove(row, row + 1, (kHistCols - 1) * sizeof(float));
        row[kHistCols - 1] = 0.0f;
    }

    // Fill rightmost column from current bid/ask levels.
    Price mid = snap.mid_price;
    for (int i = 0; i < MarketSnapshot::kDepth; ++i) {
        if (snap.bid_levels[i].qty > 0) {
            int row = kHalfRows + static_cast<int>(snap.bid_levels[i].price - mid);
            if (row >= 0 && row < kPriceRows)
                data_[row * kHistCols + (kHistCols - 1)] +=
                    static_cast<float>(snap.bid_levels[i].qty);
        }
        if (snap.ask_levels[i].qty > 0) {
            int row = kHalfRows + static_cast<int>(snap.ask_levels[i].price - mid);
            if (row >= 0 && row < kPriceRows)
                data_[row * kHistCols + (kHistCols - 1)] +=
                    static_cast<float>(snap.ask_levels[i].qty);
        }
    }

    // Recompute max for colour scale.
    max_qty_ = 1.0f;
    for (float v : data_) max_qty_ = std::max(max_qty_, v);
}

void OrderBookHeatmap::draw(const MarketSnapshot& snap) {
    if (snap.mid_price == 0) { ImGui::TextDisabled("Waiting for data..."); return; }
    shift_and_fill(snap);

    ImGui::Text("Mid: %lld  Depth heatmap (±%d ticks, last %d snaps)",
                (long long)snap.mid_price, kHalfRows, kHistCols);

    if (ImPlot::BeginPlot("##heatmap", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (snaps ago)", "Price offset (ticks)",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PushColormap(ImPlotColormap_Viridis);
        ImPlot::PlotHeatmap("Depth",
            data_.data(), kPriceRows, kHistCols,
            0.0, static_cast<double>(max_qty_),
            nullptr,
            ImPlotPoint(0,        -kHalfRows),
            ImPlotPoint(kHistCols, kHalfRows));
        ImPlot::PopColormap();
        ImPlot::EndPlot();
    }
}
