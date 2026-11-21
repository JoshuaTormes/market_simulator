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
#include "LimitOrderTrader.h"
#include <algorithm>
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
        p.half_spread_min_ticks = sample(cfg_.market_makers.half_spread_min_range, eng);
        p.vol_mult              = sample(cfg_.market_makers.vol_mult_range, eng);
        p.inventory_skew_ticks  = cfg_.market_makers.inventory_skew_ticks;
        p.adverse_sel_ticks_per_lot = cfg_.market_makers.adverse_sel_ticks_per_lot;
        p.q_soft                = cfg_.market_makers.q_soft;
        p.qty                   = static_cast<Qty>(cfg_.market_makers.mm_qty);
        p.lambda_kyle           = cfg_.market_makers.lambda_kyle;
        p.belief_decay          = cfg_.market_makers.belief_decay;
        p.impact_q_ref          = cfg_.market_makers.impact_q_ref;
        p.join_touch            = cfg_.market_makers.join_touch;
        const int pmax          = std::max(1, cfg_.market_makers.requote_period_max);
        p.requote_period        = 1 + (i % pmax);
        p.requote_phase         = i % p.requote_period;
        p.random_cadence        = cfg_.market_makers.random_cadence;
        p.accumulate_ofi        = cfg_.market_makers.accumulate_ofi;
        p.news_signal_noise     = cfg_.market_makers.news_signal_noise;
        p.news_blind            = cfg_.market_makers.news_blind;
        p.price_ref_ticks       = cfg_.price_ref_ticks;

        RiskLimits rl;
        rl.max_position = static_cast<Qty>(std::llround(2.0 * cfg_.market_makers.q_soft));

        auto mm = std::make_unique<MarketMakerAS>(
            alloc_id(), ticker_, rng_.for_consumer("MM_" + std::to_string(i)),
            p, rl, LatencyProfile{}, InformationProfile{}, bus_);
        mm->set_news_rng(rng_.for_consumer("MMN_" + std::to_string(i)));
        agents.emplace_back(std::move(mm));
    }

    // Noise traders
    for (int i = 0; i < cfg_.noise_traders.count; ++i) {
        NoiseTrader::Params p;
        p.p_act      = sample(cfg_.noise_traders.p_act_range, eng);
        p.size_mu    = sample(cfg_.noise_traders.size_mu_range, eng);
        p.size_sigma = cfg_.noise_traders.size_sigma;
        p.q_scale    = cfg_.noise_traders.q_scale;
        p.intraday   = cfg_.intraday;
        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.noise_traders.max_position);
        agents.emplace_back(std::make_unique<NoiseTrader>(
            alloc_id(), ticker_, rng_.for_consumer("NT_" + std::to_string(i)),
            p, rl));
    }

    // Informed traders — hold a position proportional to the mispricing that
    // survives their costs, and walk toward it a slice per tick.
    for (int i = 0; i < cfg_.informed_traders.count; ++i) {
        InformedTraderKyle::Params p;
        p.signal_noise_log     = sample(cfg_.informed_traders.signal_noise_range, eng);
        p.target_lots_per_tick = sample(cfg_.informed_traders.target_lots_range, eng);
        p.speed                = sample(cfg_.informed_traders.speed_range, eng);
        p.deadband_sigma       = cfg_.informed_traders.deadband_sigma;
        p.margin_ticks         = cfg_.informed_traders.margin_ticks;
        p.max_order_size       = static_cast<Qty>(cfg_.informed_traders.max_order_size);
        p.min_order            = static_cast<Qty>(cfg_.informed_traders.min_order);
        p.pareto_alpha         = cfg_.informed_traders.pareto_alpha;
        p.q_soft_frac          = cfg_.informed_traders.q_soft_frac;
        p.price_ref_ticks      = cfg_.price_ref_ticks;

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
        if (cfg_.momentum_traders.qty_override >= 0) p.qty = cfg_.momentum_traders.qty_override;
        agents.emplace_back(std::make_unique<MomentumTrader>(
            alloc_id(), ticker_, rng_.for_consumer("MOM_" + std::to_string(i)),
            p));
    }

    // Mean reverters
    for (int i = 0; i < cfg_.mean_reverters.count; ++i) {
        MeanReverterOU::Params p;
        p.entry_z = sample(cfg_.mean_reverters.entry_z_range, eng);
        p.window  = sample(cfg_.mean_reverters.window_range, eng);
        if (cfg_.mean_reverters.qty_override >= 0) p.qty = cfg_.mean_reverters.qty_override;
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
        p.intraday       = cfg_.intraday;
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
            p, rl, LatencyProfile{}, InformationProfile{}, bus_));
    }

    // Stop-loss clusters — seeded with initial ledger position when ledger is wired
    for (int i = 0; i < cfg_.stop_loss.count; ++i) {
        StopLossCluster::Params p;
        p.trigger_pct    = sample(cfg_.stop_loss.trigger_range, eng);
        p.qty            = static_cast<Qty>(sample(cfg_.stop_loss.qty_range, eng));
        p.entry_price    = static_cast<Price>(cfg_.stop_loss.entry_price_ticks);
        // Long-biased cohort: a population that is structurally long, so a
        // fall hits more stops than a rise (a stop asymmetry, see Config.h).
        p.initial_side   = (sample(ParamRange<double>{0.0, 1.0}, eng)
                            < cfg_.stop_loss.long_bias) ? Side::Buy : Side::Sell;
        p.cooldown_ticks = sample(cfg_.stop_loss.cooldown_range, eng);
        p.qty_min        = static_cast<Qty>(cfg_.stop_loss.qty_range.min_val);
        p.qty_max        = static_cast<Qty>(cfg_.stop_loss.qty_range.max_val);
        p.long_bias      = cfg_.stop_loss.long_bias;
        AgentId aid = alloc_id();
        agents.emplace_back(std::make_unique<StopLossCluster>(
            aid, ticker_, rng_.for_consumer("SL_" + std::to_string(i)), p));
        // Seed the pre-existing position so the ledger path triggers on real
        // P&L, against the allocation account so that Σ positions stays 0.
        if (ledger_) {
            Qty seed_qty = p.qty;
            if (p.initial_side == Side::Sell) seed_qty = -seed_qty;
            ledger_->seed_pair(aid, kAllocationAccount, ticker_, seed_qty, p.entry_price);
        }
    }

    // News reactors
    for (int i = 0; i < cfg_.news_reactors.count; ++i) {
        NewsReactor::Params p;
        p.reaction_lag_mean = sample(cfg_.news_reactors.lag_range, eng);
        p.sensitivity       = sample(cfg_.news_reactors.sensitivity_range, eng);
        p.base_qty          = static_cast<Qty>(cfg_.news_reactors.base_qty);
        p.impact_scale      = cfg_.news_reactors.impact_scale;
        p.hold_min_ticks    = cfg_.news_reactors.hold_range.min_val;
        p.hold_max_ticks    = cfg_.news_reactors.hold_range.max_val;
        RiskLimits rl;
        rl.max_position = static_cast<Qty>(cfg_.news_reactors.max_position);
        agents.emplace_back(std::make_unique<NewsReactor>(
            alloc_id(), ticker_, rng_.for_consumer("NR_" + std::to_string(i)),
            p, bus_, rl));
    }

    // Limit-order traders — built last, so their draws on `eng` move no other
    // agent's parameters.
    {
        const auto& c = cfg_.limit_traders;
        std::normal_distribution<double> nf(0.0, c.sigma_fund), nc(0.0, c.sigma_chart),
                                         nn(0.0, c.sigma_noise);
        for (int i = 0; i < c.count; ++i) {
            LimitOrderTrader::Params p;
            p.p_act          = sample(c.p_act_range, eng);
            p.intraday       = cfg_.intraday;
            p.g_fund         = std::abs(nf(eng));
            p.g_chart        = nc(eng);
            p.g_noise        = std::abs(nn(eng));
            p.horizon_ticks  = sample(c.horizon_range, eng);
            p.chart_window   = sample(c.chart_window_range, eng);
            p.fund_reversion_ticks = c.fund_reversion_ticks;
            p.view_sigma     = sample(c.view_sigma_range, eng);
            p.view_half_life = sample(c.view_half_life_range, eng);
            p.k_max          = c.k_max;
            p.size_mu        = c.size_mu;
            p.size_sigma     = c.size_sigma;
            p.max_size       = static_cast<Qty>(c.max_size);
            p.q_soft         = c.q_soft;
            RiskLimits rl;
            rl.max_position = static_cast<Qty>(c.max_position);
            InformationProfile ip;
            ip.sees_fundamental = true;
            agents.emplace_back(std::make_unique<LimitOrderTrader>(
                alloc_id(), ticker_, rng_.for_consumer("LOT_" + std::to_string(i)),
                p, rl, LatencyProfile{}, ip));
        }
    }

    return agents;
}
