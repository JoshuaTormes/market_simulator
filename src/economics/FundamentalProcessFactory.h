#pragma once
// Factory: creates the right IFundamentalValueProcess from FundamentalConfig.
#include "IFundamentalValueProcess.h"
#include "core/Config.h"
#include <memory>
#include <random>

// `ts` supplies the tick-to-wall-clock mapping used to convert the annualized
// volatility and per-day rates in `cfg` into per-tick quantities.
std::unique_ptr<IFundamentalValueProcess>
make_fundamental_process(const FundamentalConfig& cfg, const TimeScale& ts,
                         std::mt19937_64 rng);
