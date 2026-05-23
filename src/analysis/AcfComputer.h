#pragma once
// Autocorrelation function (ACF) computation up to a given lag.
// Uses the direct O(N·K) biased estimator — adequate for K ≤ 50, N ≤ 500k.
#include <vector>
#include <cmath>

struct AcfResult {
    std::vector<double> acf;   // acf[0]=1.0, acf[1]..acf[max_lag]
    double mean     = 0.0;
    double variance = 0.0;     // population variance (unnormalised denominator = N)
    int    n        = 0;

    // Two-sided 95% confidence band under white-noise null: ±2/sqrt(n).
    double confidence_band() const {
        return (n > 0) ? 2.0 / std::sqrt(static_cast<double>(n)) : 1.0;
    }
};

class AcfComputer {
public:
    // Returns AcfResult with acf.size() == max_lag + 1.
    // Returns empty result if x.size() <= max_lag + 1.
    static AcfResult compute(const std::vector<double>& x, int max_lag);

    // Convenience: compute ACF of |x|.
    static AcfResult compute_abs(const std::vector<double>& x, int max_lag);
};
