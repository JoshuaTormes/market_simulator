#pragma once
// 4-panel microstructure dashboard: spread, book imbalance, OFI, realized vol.
#include "marketdata/MarketSnapshot.h"
#include <vector>

class MicrostructureDashboard {
public:
    static constexpr int kMaxHistory = 500;

    void draw(const MarketSnapshot& snap);

private:
    std::vector<double> t_hist_;
    std::vector<double> spread_hist_;
    std::vector<double> imb_l1_hist_;
    std::vector<double> imb_l5_hist_;
    std::vector<double> ofi_hist_;
    std::vector<double> vol_s_hist_;  // short window
    std::vector<double> vol_m_hist_;  // medium window
    std::vector<double> vol_l_hist_;  // long window

    void push(const MarketSnapshot& snap);
};
