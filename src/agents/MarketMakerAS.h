#pragma once
// Market maker quoted in tick-units.
//
// The previous version priced its spread off a fractional-volatility formula
// borrowed from Avellaneda & Stoikov (2008) while the book lived in integer
// ticks.  The two scales never met: the half-spread collapsed to the 1-tick
// floor and inventory skew was invisible, so the maker held one side of the
// book, accumulated inventory without ever leaning against it, and traded
// mostly against the other makers.
//
// This version is *inspired by* A-S, without their risk aversion γ or terminal
// horizon: it quotes around a reservation price shifted against inventory and
// widens with volatility and with toxic flow, but the shift and the widening
// are linear rules in ticks, not the solution of the A-S utility problem.
// Inventory is controlled through *size* as well as price: the side that would
// grow the position shrinks to zero as the position approaches q_soft.
//
// Beliefs: order-flow imbalance and executed trades move the private mid with
// a *fixed* coefficient λ (lambda_kyle, square-root in |OFI|), and a public
// announcement moves it by the announced log-return.  This is not
// Glosten-Milgrom: there is no Bayesian update on the probability that the
// counterparty is informed, and λ does not respond to the informed share.
#include "AgentBase.h"
#include "core/EventBus.h"
#include "economics/NewsEvent.h"

class MarketMakerAS : public AgentBase {
public:
    struct Params {
        // Quote geometry, all in price ticks.
        double half_spread_min_ticks = 1.0;   // floor: never quote inside 1 tick
        double vol_mult              = 1.0;   // half-spread per tick of sigma
        double sigma_tick_floor      = 1.5e-4;// fractional vol floor when the book is quiet
        double inventory_skew_ticks  = 4.0;   // reservation shift at |q| = q_soft
        double adverse_sel_ticks_per_lot = 0.01; // widening per lot of |OFI|

        // Inventory control.
        double q_soft = 300.0;  // position at which the growing side stops quoting
        Qty    qty    = 100;    // lots per quote side at zero inventory

        // Belief dynamics.
        double lambda_kyle  = 0.015; // ticks of belief per lot of signed flow (at q_ref)
        double belief_decay = 0.02;  // pull toward the last executed price
        double impact_q_ref = 0.0;   // >0: square-root impact anchored at this size; 0: linear

        // Competition: if a rival's best price is better than the maker's own
        // quote but still leaves at least half_spread_min_ticks from the
        // reservation, the maker joins it — Bertrand competition compresses
        // the margin toward the floor, and the touch carries several makers.
        bool   join_touch = false;

        // Requote cadence, in ticks, and the phase within it.  Makers on a
        // single synchronized cadence stamp their own period onto the mid: with
        // every maker refreshing on the same clock the |r| autocorrelation gets
        // a trough at lag 1 and a peak at the period.  Real makers run on
        // heterogeneous latencies, so the cohort is spread over periods and
        // phases and at every tick somebody is refreshing.
        int    requote_period = 1;
        int    requote_phase  = 0;
        // false: requote every requote_period ticks exactly.  true: the gap to
        // the next requote is drawn uniformly from {1, ..., 2·period − 1}
        // (mean = period).  A fixed lattice writes the period into the mid
        // once the slow maker prices all the flow it missed in one jump.
        bool   random_cadence = false;
        // true: price the signed flow of every tick since the last requote;
        // false: only the flow of the requote tick itself.
        bool   accumulate_ofi = true;
        // Noisy reading of public news: the maker moves its belief by the
        // announced log-return times (1 + η), η ~ N(0, news_signal_noise²).
        // 0 = the maker reads the announcement exactly.  news_blind: the
        // maker ignores announcements and learns them only from the flow.
        double news_signal_noise = 0.0;
        bool   news_blind        = false;
        // > 0: every tick-denominated parameter above is read as "ticks at
        // this price" and scaled by mid / price_ref_ticks, so the maker quotes
        // the same relative spread and moves its belief by the same relative
        // amount at $10 as at $100.  0 = absolute ticks (old behaviour).
        double price_ref_ticks = 0.0;
    };

    MarketMakerAS(AgentId id, const std::string& ticker,
                  std::mt19937_64 rng, Params p,
                  RiskLimits rl = {}, LatencyProfile lp = {},
                  InformationProfile ip = {}, EventBus* bus = nullptr);

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "MarketMakerAS"; }

    // Private mid in price ticks — exposed for tests and diagnostics.
    double belief() const { return belief_; }

    // Separate stream for the news-reading noise, so the requote cadence draws
    // do not shift when the noise level changes (the factory passes its own
    // consumer id; the default engine is only for tests).
    void set_news_rng(std::mt19937_64 r) { news_rng_ = std::move(r); }

private:
    Params p_;
    bool   first_tick_ = true;
    double belief_     = 0.0;  // private mid, price ticks
    // Announced log-return not yet folded into the belief.  The bus delivers
    // news inside the news step, before this agent runs on the same tick.
    double pending_news_ = 0.0;
    double ofi_acc_      = 0.0;  // signed flow since the last requote, lots
    Tick   next_requote_ = 0;    // random cadence only
    std::uniform_real_distribution<double> gap_u_{0.0, 1.0};
    std::mt19937_64 news_rng_;
    std::normal_distribution<double> eta_n_{0.0, 1.0};
};
