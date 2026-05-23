#include "analysis/AcfComputer.h"
#include <algorithm>
#include <numeric>

AcfResult AcfComputer::compute(const std::vector<double>& x, int max_lag) {
    AcfResult res;
    int n = static_cast<int>(x.size());
    if (n <= max_lag + 1 || max_lag < 0) return res;

    res.n = n;
    res.mean = std::accumulate(x.begin(), x.end(), 0.0) / n;

    double var = 0.0;
    for (const double v : x) var += (v - res.mean) * (v - res.mean);
    res.variance = var / n;  // population variance

    res.acf.resize(max_lag + 1, 0.0);
    res.acf[0] = 1.0;

    if (var == 0.0) return res;  // constant series

    for (int lag = 1; lag <= max_lag; ++lag) {
        double cov = 0.0;
        for (int i = 0; i < n - lag; ++i)
            cov += (x[i] - res.mean) * (x[i + lag] - res.mean);
        res.acf[lag] = cov / var;  // biased estimator (denominator = var = unnorm/1)
    }
    return res;
}

AcfResult AcfComputer::compute_abs(const std::vector<double>& x, int max_lag) {
    std::vector<double> ax(x.size());
    std::transform(x.begin(), x.end(), ax.begin(),
                   [](double v) { return std::abs(v); });
    return compute(ax, max_lag);
}
