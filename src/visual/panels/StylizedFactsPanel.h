#pragma once
// Stylized facts: return histogram, ACF of returns and |returns|, Q-Q plot,
// Hill tail-index display, and live pass/fail badges for the 8 stylized facts.
// ACF computation delegates to AcfComputer (shared with the offline analysis CLI).
#include "analysis/AcfComputer.h"
#include "analysis/HillEstimator.h"
#include "marketdata/MarketSnapshot.h"
#include <vector>

class StylizedFactsPanel {
public:
    static constexpr int kMaxReturns = 1000;
    static constexpr int kMaxLag     = 20;

    void draw(const MarketSnapshot& snap);

private:
    std::vector<double> returns_;
    double prev_log_price_ = 0.0;

    // Computed series (rebuilt periodically via AcfComputer).
    AcfResult acf_ret_;
    AcfResult acf_abs_;
    HillResult hill_;

    std::vector<double> lags_;
    std::vector<double> qq_x_;
    std::vector<double> qq_y_;

    void ingest_return(const MarketSnapshot& snap);
    void recompute_stats();

    static double inv_normal_cdf(double p);
};
