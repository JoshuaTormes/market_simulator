#pragma once
// Regime indicator: coloured bar + text showing current market regime.
#include <vector>

class RegimeIndicator {
public:
    static constexpr int kMaxHistory = 500;

    // regime: -1 = N/A, 0 = quiet, 1 = elevated, 2 = high vol
    void draw(int regime, double fundamental_value, double tick);

private:
    std::vector<double> t_hist_;
    std::vector<double> regime_hist_;
    std::vector<double> fund_hist_;
};
