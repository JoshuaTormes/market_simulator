#pragma once
// Regime indicator: coloured bar + text showing current market regime.
#include <vector>

class RegimeIndicator {
public:
    static constexpr int kMaxHistory = 500;

    // regime: -1 = N/A, 0 = low_vol, 1 = high_vol, 2 = crash
    void draw(int regime, double fundamental_value, double tick);

private:
    std::vector<double> t_hist_;
    std::vector<double> regime_hist_;
    std::vector<double> fund_hist_;
};
