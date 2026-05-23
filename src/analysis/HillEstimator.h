#pragma once
// Hill estimator for the tail index α of a heavy-tailed distribution.
// Given top-k order statistics X_{(1)} ≥ ... ≥ X_{(k+1)}, the estimate is:
//   α̂ = k / Σ_{i=1}^{k} log(X_{(i)} / X_{(k+1)})
// A larger α means a thinner tail.  Financial returns typically show α ∈ [2, 5].
#include <vector>

struct HillResult {
    double alpha = 0.0;  // tail index (1/mean-excess-log)
    double xi    = 0.0;  // Pareto shape = 1/alpha
    int    k     = 0;    // order statistics used
    bool   valid = false;
};

class HillEstimator {
public:
    // Estimate right-tail index of data (uses top-k positive values).
    // k == 0 → use ceil(sqrt(n)).
    static HillResult estimate_right(const std::vector<double>& data, int k = 0);

    // Estimate tail index treating |data| as the tail variable (symmetric heavy tails).
    static HillResult estimate_abs(const std::vector<double>& data, int k = 0);
};
