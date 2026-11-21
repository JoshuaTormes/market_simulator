#pragma once
#include "Types.h"
#include "Clock.h"
#include "IntradayProfile.h"
#include <cmath>
#include <string>
#include <cstdint>

// ── Time scale ─────────────────────────────────────────────────────────────
// A tick is a fixed slice of trading time.  Every quantity with a time
// dimension — volatility, news arrival rate, agent horizons — is derived from
// these three numbers, so the model has one coherent clock instead of a
// per-component guess.  Changing seconds_per_tick rescales the whole model
// consistently.
struct TimeScale {
    double seconds_per_tick = 1.0;       // 1 tick = 1 second of trading
    double seconds_per_day  = 23400.0;   // 6.5 h US equity session
    double trading_days     = 252.0;

    double ticks_per_day()  const { return seconds_per_day / seconds_per_tick; }
    double ticks_per_year() const { return ticks_per_day() * trading_days; }

    // Per-tick volatility implied by an annualized volatility, under
    // square-root-of-time scaling.  σ_annual = 30% → σ_tick ≈ 1.24e-4.
    double per_tick_vol(double sigma_annual) const {
        return sigma_annual / std::sqrt(ticks_per_year());
    }

    // Convert a per-day rate (e.g. news announcements per session) to per tick.
    double per_tick_rate(double per_day) const { return per_day / ticks_per_day(); }
};

// ── Per-agent-type population ranges ──────────────────────────────────────

template<typename T>
struct ParamRange {
    T min_val;
    T max_val;
};

struct MarketMakerParams {
    int count  = 3;
    // 200 lots per side: quoted depth has to exceed a typical aggressive order
    // (institutional children cap at 200) or one market order empties the book
    // and the mid stops existing until the next requote.
    int mm_qty = 200;
    // Quote geometry in price ticks — the book is integer, so the maker is too.
    // Three identical makers are one maker: same inputs, same quote, and time
    // priority hands every fill to the first id.  The floor and the vol
    // multiplier are drawn per maker so the book has layers and the makers
    // compete on the spread.
    ParamRange<double> half_spread_min_range = {0.3, 1.1};
    ParamRange<double> vol_mult_range        = {0.25, 0.55};
    double inventory_skew_ticks  = 4.0;
    double adverse_sel_ticks_per_lot = 0.0030;
    // 600 lots = three full-size quotes: the maker must be able to absorb a few
    // complete fills before it withdraws a side, or the book goes one-sided by
    // construction rather than because liquidity ran out.
    double q_soft                = 600.0;
    // Belief dynamics.  0.045 since the makers price all the flow since their
    // last requote (accumulate_ofi): at 0.06, calibrated when the slow makers
    // saw one tick in three, the mid ran at 0.375 annual vol against 0.287 for V.
    double lambda_kyle  = 0.045;
    double belief_decay = 0.02;
    // Concave impact: belief moves by lambda·sign(OFI)·sqrt(|OFI|·q_ref), so a
    // q_ref-lot order moves exactly lambda·q_ref ticks and a 4x larger one
    // only 2x more — the square-root law (Bouchaud, Farmer & Lillo 2009).
    // Kyle's linear rule is the one-shot equilibrium; aggregated split orders
    // are concave.  <= 0 restores the linear update.
    double impact_q_ref = 50.0;
    // Competition between makers.  The per-tick random arrival order in
    // AgentRunner is what keeps one maker from taking >90% of maker volume by
    // time priority alone.  join_touch (join a rival's tighter quote when it
    // still covers the own floor) spreads volume further but stacks depth at
    // the touch: measured, it flattens the 1-tick volatility clustering and
    // slows short-horizon price discovery, so it is off by default.
    bool   join_touch = false;

    // Longest mean requote interval in the maker cohort, in ticks: maker i
    // averages 1 + (i % requote_period_max) ticks between requotes.
    int    requote_period_max = 3;
    // Random cadence: the gap to the next requote is drawn uniformly from
    // {1, ..., 2·period − 1}.  Any fixed cadence is written into the mid.  With
    // every maker on period 1, ACF(|r|) had a trough at lag 1 and peaks at lags
    // 2-3; staggering the makers over fixed periods 1/2/3 only moved the
    // artifact onto a period-6 lattice (ACF(|r|) 0.13 / 0.23 / 0.14 at lags
    // 1-3, 0.21 at lag 6; P(mid moves) 0.55 to 0.89 by tick phase).
    bool   random_cadence = true;
    // Price the signed flow of every tick since the last requote, not only the
    // requote tick's.  Without it the period-1 maker was the only one absorbing
    // the whole flow and lost ~US$300k per 60 sessions to adverse selection.
    bool   accumulate_ofi = true;
    // See MarketMakerAS::Params::news_signal_noise / news_blind.
    double news_signal_noise = 0.0;
    bool   news_blind        = false;
    // Propagator split: how much of the flow's push stays in the price and how
    // fast the rest decays.  Order flow is persistent (orders are split), so a
    // fully permanent update turns every execution schedule into a trend and
    // the return ACF blows up; a decaying transient component is what lets the
    // flow stay autocorrelated while returns do not.
    double permanent_frac = 1.0;
    double impact_decay   = 0.0;
};

struct NoiseTraderParams {
    int count = 25;
    ParamRange<double> p_act_range    = {0.02, 0.10};
    // With size_sigma = 1.5 the lognormal mean is exp(mu + sigma^2/2), so this
    // range puts E[size] between 8 and 23 lots, ~15 on average.
    ParamRange<double> size_mu_range  = {1.0,  2.0};
    double size_sigma                 = 1.5;
    // Inventory scale of the side tilt.  Uninformed does not mean unbounded:
    // without this the cohort random-walks into its position limit and its
    // rejected flow becomes a spurious directional push.
    double q_scale                    = 500.0;
    int    max_position               = 5000;
};

struct InformedTraderParams {
    // Two desks, not three: informed traders who share a signal race each
    // other into the same price and compete their own edge away, which is the
    // Kyle result as the number of insiders grows.
    int    count             = 2;
    // Signal quality differs across desks — identical signals would make the
    // cohort one agent with three ids.  The range brackets the old single
    // value of 5e-4 (5 ticks of error at $100).
    ParamRange<double> signal_noise_range = {3e-4, 9e-4};
    // Target position: q* = kappa · (gap beyond the costs), and each tick the
    // trader closes `speed` of the distance to it.  Kyle's insider spreads its
    // trades over time; a fast schedule pays its own impact and turns the
    // cohort's flow into a price trend, which is what breaks the return ACF.
    // The ranges make the two desks differ in appetite and in urgency.
    ParamRange<double> target_lots_range = {40.0, 80.0};
    ParamRange<double> speed_range       = {0.015, 0.030};
    // Deadband in sigmas of the own signal noise: a gap inside the error bar is
    // noise, and trading noise pays the spread to the makers for nothing.
    double deadband_sigma    = 1.0;
    double margin_ticks      = 1.0;   // gap must beat half-spread by this much
    int    max_order_size    = 200;
    int    min_order         = 5;     // slices below this are not worth the spread
    int    max_position      = 4000;
    double pareto_alpha      = 1.5;   // Pareto tail exponent for order sizing
    double q_soft_frac       = 0.6;   // own risk budget as a fraction of max_position
};

struct MomentumParams {
    // count=3: three trend followers against two mean reverters.  The pair of
    // opposing strategies is what keeps the return ACF near zero while still
    // producing volatility clustering; a single momentum trader with no
    // counterparty just adds one-sided herding.  Measured at 2 against the
    // staggered maker cadence: the lag-1 return ACF did not improve and the
    // makers lost a seed, so the pair stays 3 vs 2.
    int count = 3;
    ParamRange<int>    fast_range     = {5,  15};
    ParamRange<int>    slow_range     = {20, 50};
    // Ablation switch: >= 0 replaces the order size after every parameter
    // draw, so 0 silences the cohort without shifting anyone's RNG stream
    // (count = 0 would skip draws and change every agent built after it).
    int qty_override = -1;
};

struct MeanReverterParams {
    // count=2: the counterweight to the momentum cohort (see MomentumParams).
    // A third reverter was measured against the staggered maker cadence and
    // made the lag-1 return ACF worse (0.062 vs 0.052), not better.
    int count = 2;
    ParamRange<double> entry_z_range  = {1.5, 3.0};
    ParamRange<int>    window_range   = {20,  60};
    int qty_override = -1;   // see MomentumParams::qty_override
};

struct ValueInvestorParams {
    int count = 0;
    ParamRange<double> k_range        = {0.01, 0.05};
};

struct InstitutionalParams {
    int    count          = 2;      // two desks: order splitting drives fat tails
    // Parent arrival process — a desk that receives one order per run stops
    // contributing flow after the first hundred ticks.
    double arrival_lambda = 0.002;  // P(new parent | idle tick) ≈ one per 500 ticks
    int    parent_min     = 200;    // Pareto parent size, truncated to
    int    parent_max     = 3000;   // [parent_min, parent_max]
    double parent_alpha   = 1.5;    // Pareto tail exponent for parent size
    double q_scale        = 1500.0; // inventory scale of the parent-side tilt
    int    slices         = 20;
    int    ticks_between  = 5;
    double pareto_alpha   = 1.5;    // Pareto tail exponent for child order sizing
    int    max_child_qty  = 200;    // hard cap per child order
    int    max_position   = 4000;
};

struct StopLossParams {
    int   count                       = 6;
    int64_t entry_price_ticks         = 10000; // reference price for seeded positions
    // 1.5-4%: with the price now tracking the fundamental to within ~0.2% the
    // old 4-8% band was almost never reached, so no cascade ever formed.
    ParamRange<double> trigger_range  = {0.015, 0.04};
    ParamRange<int>    qty_range      = {10,   75};  // 6×75=450 < 600 MM depth: avoids bid depletion
    // Ticks flat after a stop fires before a new position is established.
    ParamRange<int>    cooldown_range = {200, 800};
    // Fraction of the cluster that is long.  Real equity investors are
    // structurally long, so a fall triggers far more stops than a rise of the
    // same size.  This is a stop asymmetry, not the leverage effect: Black
    // (1976) is about a falling stock raising the firm's leverage and so its
    // volatility, and nothing here models a balance sheet.  It is one source
    // of negative return skew; 0.5 would make falls and rises mirror images.
    double long_bias                  = 0.7;
};

struct NewsReactorParams {
    // lag_range = [0,1]: short lags, so the cohort starts reacting within a tick
    // of the announcement.  Each reactor then fires exactly once, at a moment
    // drawn inside the event window — the heterogeneity across the 8 reactors
    // is what spreads the impact over several ticks, not a repeated order from
    // each of them.
    int count = 8;
    int base_qty = 50;                             // lots per reaction tick for a typical headline
    ParamRange<double> lag_range      = {0, 1};
    ParamRange<double> sensitivity_range = {0.5, 2.0};
    // Reference impact base_qty is quoted against — keep in sync with
    // NewsConfig::impact_scale, or reactions collapse to the 1-lot floor.
    double impact_scale = 0.00273;
    int    max_position = 2000;
    // Holding horizon per reaction, log-uniform in ticks; {0, 0} = never close.
    // The makers already reprice the headline through the EventBus, so a
    // reaction is a short-horizon bet, not an investment: kept forever, the
    // cohort's inventory random-walked to ~-7000 lots in 60 sessions,
    // saturated the informed traders and left the gap to V unanchored.
    ParamRange<double> hold_range = {300.0, 3600.0};
};

struct LimitOrderTraderParams {
    // Patient traders who post where they expect the price to go and wait
    // (Chiarella & Iori 2002).  Without them every non-maker used market
    // orders and the whole touch belonged to the three makers by construction.
    int count = 20;
    // Arrival per tick at mid-session: 20 agents × ~1.25% ≈ 0.25 orders/s,
    // of the order of the noise cohort's market orders.
    ParamRange<double> p_act_range = {0.005, 0.02};
    // Expectation weights: g_fund ~ |N(0, s_f)|, g_chart ~ N(0, s_c) (negative
    // = contrarian), g_noise ~ |N(0, s_n)|, normalised by their sum.  Equal
    // scales: no style dominates the cohort, each agent is a different mix.
    double sigma_fund  = 1.0;
    double sigma_chart = 1.0;
    double sigma_noise = 1.0;
    // Horizon τ in seconds of the TimeScale (1 tick = 1 s): also the TTL.
    // From half a minute to ten minutes.
    ParamRange<int>    horizon_range      = {30, 600};
    ParamRange<int>    chart_window_range = {20, 300};
    // Time over which a trader expects the gap to V to close: the measured gap
    // half-life is a few hundred seconds, an hour is the patient end.
    double fund_reversion_ticks = 3600.0;
    // Private view of V: AR(1) log error, 0.2-0.6% std, half-life of 0.5-2 h.
    // The informed desks' error is i.i.d. and ≤ 0.09% per tick; this one does
    // not average out, so the cohort is a weak, stale fundamentalist.
    ParamRange<double> view_sigma_range     = {0.002, 0.006};
    ParamRange<double> view_half_life_range = {1800.0, 7200.0};
    // Order placed k ~ U(0, k_max) short of the expected price.  20 bp at
    // $100 is 20 ticks, a few spreads: most orders rest near the touch, some deeper.
    double k_max      = 0.002;
    double size_mu    = 2.0;    // lognormal: E[size] = exp(2.5) ≈ 12 lots
    double size_sigma = 1.0;
    int    max_size   = 100;
    double q_soft     = 500.0;  // growing side faded to zero at |q| = q_soft
    int    max_position = 1000;
};

// ── Population config ──────────────────────────────────────────────────────

struct PopulationConfig {
    // Session-shaped activity: 1.8x at the open and the close, 0.6x at
    // mid-day.  Applied to the arrival rates of the discretionary cohorts.
    IntradayProfile     intraday{ .floor_mult = 0.6, .peak_mult = 1.8 };
    // > 0: maker and informed tick parameters are calibrated at this price and
    // scale with the mid (relative units).  0 = absolute ticks.  In absolute
    // ticks a falling price meant a wider relative spread and a larger
    // relative belief step, which accelerated the fall until bid <= 0.
    double              price_ref_ticks = 10000.0;
    MarketMakerParams   market_makers;
    NoiseTraderParams   noise_traders;
    InformedTraderParams informed_traders;
    MomentumParams      momentum_traders;
    MeanReverterParams  mean_reverters;
    ValueInvestorParams value_investors;
    InstitutionalParams institutionals;
    StopLossParams      stop_loss;
    NewsReactorParams   news_reactors;
    LimitOrderTraderParams limit_traders;
};

// ── Fundamental value process config ──────────────────────────────────────

enum class FundamentalProcessType { GBM, OU, JumpDiffusion, RegimeSwitching };

struct FundamentalConfig {
    FundamentalProcessType type = FundamentalProcessType::RegimeSwitching;
    double initial_value        = 100.0;

    // Annualized volatility of the fundamental — the one volatility knob.
    // 30% is the typical range for a large-cap equity.  The per-tick sigma
    // used by every process is derived from this via TimeScale, so the value
    // no longer depends on what a "tick" happens to mean.
    double sigma_annual        = 0.30;
    // Drift of the log fundamental, per year.  Zero: a martingale.  Any
    // non-zero value here is a claim about expected returns, not a fix for a
    // process that collapses.
    double mu_annual           = 0.0;
    // Variance per tick that public announcements inject into the fundamental
    // through apply_shock().  Merton (1976): the total variance of the value
    // process is the diffusive part plus the jump part, so an announcement
    // stream is not free — it has to come out of the sigma_annual budget or
    // sigma_annual stops being the volatility of anything.  The callers fill
    // this from NewsConfig::variance_per_tick(); zero means "no news", which
    // is what the unit tests construct.
    double news_var_per_tick   = 0.0;

    // OU only: mean-reversion speed per tick, and long-run mean.
    double kappa                = 0.05;
    double theta                = 100.0;
    // Jump-diffusion only: jumps per day and log-jump size.
    double jumps_per_day        = 2.0;
    double jump_mean            = 0.0;
    double jump_sigma           = 0.03;

    // Regime switching.
    double nu                   = 3.5;    // Student-t degrees of freedom for innovations
    bool   gaussian_innovations = false;  // if true, use N(0,1) instead of Student-t
    // Regimes are multipliers on the base per-tick sigma, not absolute levels:
    // quiet / elevated / high vol.  The high-vol state is 6x normal
    // volatility with zero drift: a volatility state, not a crash, and not a
    // different asset.  The base sigma is divided by sqrt(E[m^2]) under the
    // chain's stationary distribution, so the *unconditional* volatility of V
    // is sigma_annual — otherwise the multipliers silently double it.
    double regime_vol_mult[3]   = {1.0, 2.5, 6.0};
    // Expected sojourn time in each regime, in seconds of trading.  Volatility
    // states persist for hours (Andersen & Bollerslev 1997), not for the tens
    // of seconds a per-tick matrix written by hand ends up implying: 2 h quiet,
    // 30 min elevated, 3 min high vol.  The transition matrix is derived from
    // these and from TimeScale, so it survives a change of tick length.
    double regime_duration_sec[3] = {7200.0, 1800.0, 180.0};
    // Where the exit mass goes (row = from; diagonal ignored, rows sum to 1):
    // a quiet market mostly turns elevated before it reaches high vol, and
    // high vol mostly decays into the elevated state rather than to quiet.
    double regime_exit_split[3][3] = {
        {0.0, 0.8, 0.2},
        {0.8, 0.0, 0.2},
        {0.3, 0.7, 0.0}
    };
};

// ── News process config ────────────────────────────────────────────────────

struct NewsConfig {
    // Public announcements per trading session.  8/day is roughly what a
    // liquid single name sees (earnings, guidance, sector and macro prints).
    double events_per_day       = 8.0;
    double impact_df            = 3.0;    // Student-t degrees of freedom for magnitude
    // Scale of the announcement's log-impact on the fundamental.  With t(3)
    // tails a typical headline moves the value a few tens of basis points and
    // a rare one a couple of percent — the size of a real earnings surprise.
    // The level is set by the variance budget: announcements take ~50% of the
    // fundamental's total variance, the diffusion the other half.  That split
    // is what a jump-diffusion decomposition of realized variance finds for a
    // single name at high frequency (Andersen, Bollerslev & Diebold 2007) — and
    // it is load-bearing here, because the jump half is what supplies the tail
    // mass: at a 25% share the Hill index climbed to 6.3 and Fact 6 failed.
    // At the old 0.4% the stream alone carried 108% of a 30% budget and the
    // realized volatility of V came out at 43%.
    double impact_scale         = 0.00273;
    int    duration_ticks       = 5;      // ticks of sustained reactor firing per event
    double dispersion           = 0.5;    // cross-agent heterogeneity

    double lambda_per_tick(const TimeScale& ts) const {
        return ts.per_tick_rate(events_per_day);
    }

    // Variance the announcement stream adds to log V per tick.  Impacts are
    // sign * impact_scale * |t(df)| and arrive Poisson, so the per-tick
    // second moment is lambda * impact_scale^2 * var[t(df)].  Needs df > 2 for
    // that variance to exist; below that the stream has no finite budget and
    // contributes nothing measurable to subtract.
    double variance_per_tick(const TimeScale& ts) const {
        if (impact_df <= 2.0) return 0.0;
        return lambda_per_tick(ts) * impact_scale * impact_scale
             * (impact_df / (impact_df - 2.0));
    }
};

// ── Root simulation config ─────────────────────────────────────────────────

struct SimulationConfig {
    uint64_t  seed              = 42;
    uint64_t  max_ticks         = 100'000;
    ClockMode clock_mode        = ClockMode::AsFastAsPossible;
    TimeScale time;                          // 1 tick = 1 s of trading by default
    double    accel_factor      = 1.0;
    std::string ticker          = "AAPL";
    double    tick_size         = 0.01;
    int64_t   lot_size          = 1;
    int64_t   initial_price_ticks = 10'000;  // $100.00 at tick_size=0.01
    int64_t   total_outstanding = 0;         // 0 = auto from population
    int       publish_interval_ticks = 1;
    PopulationConfig population;
    FundamentalConfig fundamental;
    NewsConfig news;
    // Exchange fees in bp of notional (FeeModel).  Zero by default: no agent
    // prices the fee, so it only moves surplus to the exchange (Etapa 12).
    double    taker_fee_bp      = 0.0;
    double    maker_rebate_bp   = 0.0;
    std::string log_output_path = "sim.bin.log";
};
