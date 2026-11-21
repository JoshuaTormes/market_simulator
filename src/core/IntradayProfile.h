#pragma once
#include "Types.h"
#include <algorithm>

// Intraday activity seasonality — the U shape.
//
// Trading volume, volatility and spreads are not flat across the session:
// they are high at the open, fall to a mid-day trough and rise again into the
// close (Admati & Pfleiderer 1988; Wood, McInish & Ord 1985).  Discretionary
// liquidity traders cluster where they can hide in other people's volume, and
// the clustering is self-reinforcing.
//
// The multiplier is a parabola in the fraction of the session elapsed:
//
//     m(tau) = floor + (peak - floor) * (2*tau - 1)^2,   tau in [0, 1)
//
// so m(0) = m(1) = peak at the open and the close, and m(0.5) = floor at
// mid-day.  Agents scale their arrival probability by it; the mean over the
// session is floor + (peak - floor)/3, which is what keeps daily volume
// comparable to the flat case.
struct IntradayProfile {
    double floor_mult   = 1.0;      // mid-day activity, relative
    double peak_mult    = 1.0;      // open/close activity, relative
    double ticks_per_day = 23400.0; // must match TimeScale::ticks_per_day()

    double multiplier(Tick t) const {
        if (ticks_per_day <= 0.0) return 1.0;
        const double tau = static_cast<double>(t % static_cast<Tick>(ticks_per_day))
                         / ticks_per_day;
        const double u = 2.0 * tau - 1.0;
        return floor_mult + (peak_mult - floor_mult) * u * u;
    }
};
