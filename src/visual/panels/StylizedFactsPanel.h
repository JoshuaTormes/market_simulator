#pragma once
// Stylized facts: return histogram, ACF of returns and |returns|, Q-Q plot vs normal.
#include "marketdata/MarketSnapshot.h"
#include <vector>

class StylizedFactsPanel {
public:
    static constexpr int kMaxReturns = 1000;
    static constexpr int kMaxLag     = 20;

    void draw(const MarketSnapshot& snap);

private:
    std::vector<double> returns_;    // log-returns history
    double prev_log_price_ = 0.0;

    // Computed series (rebuilt periodically)
    std::vector<double> acf_ret_;    // ACF of returns
    std::vector<double> acf_abs_;    // ACF of |returns|
    std::vector<double> lags_;
    std::vector<double> qq_x_;       // theoretical normal quantiles
    std::vector<double> qq_y_;       // empirical return quantiles

    void ingest_return(const MarketSnapshot& snap);
    void recompute_stats();
    static double inv_normal_cdf(double p);
};
