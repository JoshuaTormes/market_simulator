#include "AgentFactory.h"
#include "MarketMakerAS.h"
#include "InformedTraderKyle.h"
#include "NoiseTrader.h"
#include "MomentumTrader.h"
#include "MeanReverterOU.h"
#include "ValueInvestor.h"
#include "InstitutionalExecutor.h"
#include "StopLossCluster.h"
#include "NewsReactor.h"
#include <cmath>
#include <random>
#include <type_traits>

AgentFactory::AgentFactory(const PopulationConfig& cfg, RngService& rng,
                           const std::string& ticker, EventBus* bus,
                           PositionLedger* ledger)
    : cfg_(cfg), rng_(rng), ticker_(ticker), bus_(bus), ledger_(ledger)
{}

template<typename T>
T AgentFactory::sample(const ParamRange<T>& range, std::mt19937_64& eng) {
    if constexpr (std::is_integral_v<T>) {
        std::uniform_int_distribution<T> dist(range.min_val, range.max_val);
        return dist(eng);
    } else {
        std::uniform_real_distribution<T> dist(range.min_val, range.max_val);
        return dist(eng);
    }
}

std::vector<std::unique_ptr<IAgent>> AgentFactory::create_all() {
    std::mt19937_64 eng = rng_.for_consumer("AgentFactory");
    std::vector<std::unique_ptr<IAgent>> agents;

    // Market makers — Bayesian: infer value from order flow, not from fundamental.
    // max_position is 2·q_soft so the size fade, not the risk gate, is what
    // stops them; a maker rejected by the gate simply vanishes from one side.
    for (int i = 0; i < cfg_.market_makers.count; ++i) {
        MarketMakerAS::Params p;
        p.half_spread_min_ticks = cfg_.market_makers.half_spread_min_ticks;
        p.vol_mult              = cfg_.market_makers.vol_mult;
        p.inventory_skew_ticks  = cfg_.market_makers.inventory_skew_ticks;
        p.adverse_sel_ticks_per_lot = cfg_.market_makers.adverse_sel_ticks_per_lot;
        p.q_soft                = cfg_.market_makers.q_soft;
        p.qty                   = static_cast<Qty>(cfg_.market_makers.mm_qty);
        p.lambda_kyle           = cfg_.market_makers.lambda_kyle;
        p.belief_decay          = cfg_.market_makers.belief_decay;

        RiskLimits rl;
        rl.max_position = static_cast<Qty>(std::llround(2.0 * cfg_.market_makers.q_soft));

        agents.emplace_back(std::make_unique<MarketMakerAS>(
            alloc_id(), ticker_, rng_.for_consumer("MM_" + std::to_string(i)),
            p, rl, LatencyProfile{}, InformationProfile{}, bus_));
    }

    // Noise traders
    for (int i = 0; i < cfg_.noise_traders.count; ++i) {
        NoiseTrader::Params p;
        p.p_act      = sample(cfg_.noise_traders.p_act_range, eng);
        p.size_mu    = sample(cfg_.noise_traders.size_mu_range, eng);
        p.size_sigma = cfg_.noise_traders.size_sigma;
        p.q_scale    = cfg_.noise_traders.q_scale;
        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.noise_traders.max_position);
        agents.emplace_back(std::make_unique<NoiseTrader>(
            alloc_id(), ticker_, rng_.for_consumer("NT_" + std::to_string(i)),
            p, rl));
    }

    // Informed traders — trade the gap between a noisy private value and the
    // quoted mid, and unwind once the gap fits inside the spread.
    for (int i = 0; i < cfg_.informed_traders.count; ++i) {
        InformedTraderKyle::Params p;
        p.signal_noise_log = cfg_.informed_traders.signal_noise_log;
        p.lambda_inv       = cfg_.informed_traders.lambda_inv;
        p.margin_ticks     = cfg_.informed_traders.margin_ticks;
        p.max_order_size   = static_cast<Qty>(cfg_.informed_traders.max_order_size);
        p.unwind_qty       = static_cast<Qty>(cfg_.informed_traders.unwind_qty);
        p.pareto_alpha     = cfg_.informed_traders.pareto_alpha;
        p.q_soft_frac      = cfg_.informed_traders.q_soft_frac;

        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.informed_traders.max_position);

        InformationProfile ip;
        ip.sees_fundamental = true;

        agents.emplace_back(std::make_unique<InformedTraderKyle>(
            alloc_id(), ticker_, rng_.for_consumer("IT_" + std::to_string(i)),
            p, rl, LatencyProfile{}, ip));
    }

    // Momentum traders
    for (int i = 0; i < cfg_.momentum_traders.count; ++i) {
        MomentumTrader::Params p;
        p.fast_window = sample(cfg_.momentum_traders.fast_range, eng);
        p.slow_window = sample(cfg_.momentum_traders.slow_range, eng);
        agents.emplace_back(std::make_unique<MomentumTrader>(
            alloc_id(), ticker_, rng_.for_consumer("MOM_" + std::to_string(i)),
            p));
    }

    // Mean reverters
    for (int i = 0; i < cfg_.mean_reverters.count; ++i) {
        MeanReverterOU::Params p;
        p.entry_z = sample(cfg_.mean_reverters.entry_z_range, eng);
        p.window  = sample(cfg_.mean_reverters.window_range, eng);
        agents.emplace_back(std::make_unique<MeanReverterOU>(
            alloc_id(), ticker_, rng_.for_consumer("MR_" + std::to_string(i)),
            p));
    }

    // Value investors — act on own noisy belief about fair value (no direct fundamental access)
    for (int i = 0; i < cfg_.value_investors.count; ++i) {
        ValueInvestor::Params p;
        p.kappa = sample(cfg_.value_investors.k_range, eng);
        agents.emplace_back(std::make_unique<ValueInvestor>(
            alloc_id(), ticker_, rng_.for_consumer("VI_" + std::to_string(i)),
            p, RiskLimits{}, LatencyProfile{}, InformationProfile{}));
    }

    // Institutional desks — Poisson parent arrivals, Pareto parent and child
    // sizes: order splitting is what makes trade flow heavy-tailed and its sign
    // persistent.
    for (int i = 0; i < cfg_.institutionals.count; ++i) {
        InstitutionalExecutor::Params p;
        p.arrival_lambda = cfg_.institutionals.arrival_lambda;
        p.parent_min     = static_cast<Qty>(cfg_.institutionals.parent_min);
        p.parent_max     = static_cast<Qty>(cfg_.institutionals.parent_max);
        p.parent_alpha   = cfg_.institutionals.parent_alpha;
        p.q_scale        = cfg_.institutionals.q_scale;
        p.total_slices   = cfg_.institutionals.slices;
        p.ticks_between  = cfg_.institutionals.ticks_between;
        p.pareto_alpha   = cfg_.institutionals.pareto_alpha;
        p.max_child_qty  = static_cast<Qty>(cfg_.institutionals.max_child_qty);
        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.institutionals.max_position);
        agents.emplace_back(std::make_unique<InstitutionalExecutor>(
            alloc_id(), ticker_, rng_.for_consumer("IE_" + std::to_string(i)),
            p, rl));
    }

    // Stop-loss clusters — seeded with initial ledger position when ledger is wired
    for (int i = 0; i < cfg_.stop_loss.count; ++i) {
        StopLossCluster::Params p;
        p.trigger_pct    = sample(cfg_.stop_loss.trigger_range, eng);
        p.qty            = static_cast<Qty>(sample(cfg_.stop_loss.qty_range, eng));
        p.entry_price    = static_cast<Price>(cfg_.stop_loss.entry_price_ticks);
        p.initial_side   = (i % 2 == 0) ? Side::Buy : Side::Sell;
        p.cooldown_ticks = sample(cfg_.stop_loss.cooldown_range, eng);
        p.qty_min        = static_cast<Qty>(cfg_.stop_loss.qty_range.min_val);
        p.qty_max        = static_cast<Qty>(cfg_.stop_loss.qty_range.max_val);
        AgentId aid = alloc_id();
        agents.emplace_back(std::make_unique<StopLossCluster>(
            aid, ticker_, rng_.for_consumer("SL_" + std::to_string(i)), p));
        // Seed the pre-existing position so the ledger path triggers on real P&L.
        if (ledger_) {
            Qty seed_qty = p.qty;
            if (p.initial_side == Side::Sell) seed_qty = -seed_qty;
            ledger_->seed(aid, ticker_, seed_qty, p.entry_price);
        }
    }

    // News reactors
    for (int i = 0; i < cfg_.news_reactors.count; ++i) {
        NewsReactor::Params p;
        p.reaction_lag_mean = sample(cfg_.news_reactors.lag_range, eng);
        p.sensitivity       = sample(cfg_.news_reactors.sensitivity_range, eng);
        p.base_qty          = static_cast<Qty>(cfg_.news_reactors.base_qty);
        p.impact_scale      = cfg_.news_reactors.impact_scale;
        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.news_reactors.max_position);
        agents.emplace_back(std::make_unique<NewsReactor>(
            alloc_id(), ticker_, rng_.for_consumer("NR_" + std::to_string(i)),
            p, bus_, rl));
    }

    return agents;
}
