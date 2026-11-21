#include "CandleVolumeVwap.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>

void CandleVolumeVwap::ingest(const MarketSnapshot& snap) {
    double mid = static_cast<double>(snap.mid_price);
    if (mid == 0.0) return;

    if (agg_count_ == 0) {
        agg_open_ = mid;
        agg_high_ = mid;
        agg_low_  = mid;
        agg_t_    = static_cast<double>(snap.tick);
    }
    agg_high_ = std::max(agg_high_, mid);
    agg_low_  = std::min(agg_low_,  mid);
    ++agg_count_;

    if (agg_count_ >= kCandleAgg) {
        Candle c;
        c.t     = agg_t_;
        c.open  = agg_open_;
        c.high  = agg_high_;
        c.low   = agg_low_;
        c.close = mid;
        c.vwap  = snap.vwap[0];
        c.vol   = std::abs(snap.order_flow_imbalance) + 1e-9;
        if (static_cast<int>(candles_.size()) >= kMaxCandles)
            candles_.erase(candles_.begin());
        candles_.push_back(c);
        agg_count_ = 0;
        rebuild_arrays();
    }
}

void CandleVolumeVwap::rebuild_arrays() {
    int n = static_cast<int>(candles_.size());
    xs_.resize(n); lo_.resize(n); hi_.resize(n);
    close_.resize(n); vwap_y_.resize(n); vol_y_.resize(n);
    for (int i = 0; i < n; ++i) {
        xs_[i]     = candles_[i].t;
        lo_[i]     = candles_[i].low;
        hi_[i]     = candles_[i].high;
        close_[i]  = candles_[i].close;
        vwap_y_[i] = candles_[i].vwap;
        vol_y_[i]  = candles_[i].vol;
    }
}

void CandleVolumeVwap::draw(const MarketSnapshot& snap) {
    ingest(snap);
    int n = static_cast<int>(candles_.size());

    ImGui::Text("Candles (agg=%d)  VWAP[0]=%.4f", kCandleAgg,
                n > 0 ? vwap_y_.back() : 0.0);

    if (n < 2) { ImGui::TextDisabled("Building candles..."); return; }

    if (ImPlot::BeginPlot("##candle", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Tick", "Price",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y2, "OFI Vol",
                          ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);

        // High-low shaded region (candle body range)
        ImPlot::PlotShaded("HL Range",
            xs_.data(), lo_.data(), hi_.data(), n);

        // Close price line
        ImPlot::PlotLine("Close", xs_.data(), close_.data(), n);

        // VWAP line
        ImPlot::PlotLine("VWAP", xs_.data(), vwap_y_.data(), n);

        // Volume bars on Y2
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        ImPlot::PlotBars("OFI Vol", xs_.data(), vol_y_.data(), n,
                         static_cast<double>(kCandleAgg) * 0.8);

        ImPlot::EndPlot();
    }
}
