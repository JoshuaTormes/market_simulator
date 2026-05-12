#pragma once
// OHLC candles built from mid_price + VWAP line + OFI-derived volume bars.
#include "marketdata/MarketSnapshot.h"
#include <vector>

class CandleVolumeVwap {
public:
    static constexpr int kCandleAgg  = 5;    // snapshots per candle
    static constexpr int kMaxCandles = 200;

    void draw(const MarketSnapshot& snap);

private:
    struct Candle {
        double t;    // tick of candle open
        double open, high, low, close;
        double vwap;
        double vol;  // |OFI| as volume proxy
    };

    std::vector<Candle> candles_;
    std::vector<double> xs_, lo_, hi_, close_, vwap_y_, vol_y_;

    // Accumulator for current in-progress candle.
    int    agg_count_ = 0;
    double agg_open_  = 0.0, agg_high_ = 0.0, agg_low_ = 0.0;
    double agg_t_     = 0.0;

    void ingest(const MarketSnapshot& snap);
    void rebuild_arrays();
};
