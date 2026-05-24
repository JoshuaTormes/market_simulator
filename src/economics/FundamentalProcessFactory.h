#pragma once
// Factory: creates the right IFundamentalValueProcess from FundamentalConfig.
#include "IFundamentalValueProcess.h"
#include "core/Config.h"
#include <memory>
#include <random>

std::unique_ptr<IFundamentalValueProcess>
make_fundamental_process(const FundamentalConfig& cfg, std::mt19937_64 rng);
