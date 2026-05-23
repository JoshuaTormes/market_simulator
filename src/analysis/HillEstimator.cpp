#include "analysis/HillEstimator.h"
#include <algorithm>
#include <cmath>

static HillResult hill_core(std::vector<double> vals, int k) {
    HillResult res;
    // Keep only strictly positive values.
    vals.erase(std::remove_if(vals.begin(), vals.end(),
                              [](double v) { return v <= 0.0; }),
               vals.end());
    int n = static_cast<int>(vals.size());
    if (n < 4) return res;

    std::sort(vals.begin(), vals.end(), std::greater<double>());

    if (k <= 0) k = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (k >= n) k = n - 1;

    // X_{(k+1)} is the (k+1)-th largest value (threshold).
    double threshold = vals[k];
    if (threshold <= 0.0) return res;

    double sum_log = 0.0;
    for (int i = 0; i < k; ++i) {
        double ratio = vals[i] / threshold;
        if (ratio <= 1.0) { k = i; break; }
        sum_log += std::log(ratio);
    }
    if (k == 0 || sum_log == 0.0) return res;

    res.valid = true;
    res.k     = k;
    res.xi    = sum_log / k;
    res.alpha = 1.0 / res.xi;
    return res;
}

HillResult HillEstimator::estimate_right(const std::vector<double>& data, int k) {
    return hill_core(data, k);
}

HillResult HillEstimator::estimate_abs(const std::vector<double>& data, int k) {
    std::vector<double> av(data.size());
    std::transform(data.begin(), data.end(), av.begin(),
                   [](double v) { return std::abs(v); });
    return hill_core(av, k);
}
