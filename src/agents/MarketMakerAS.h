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
// This version keeps the A-S logic — quote around a reservation price shifted
// against inventory, widen with volatility and with toxic flow — but every
// term is expressed in ticks, and inventory is controlled through *size* as
// well as price: the side that would grow the position shrinks to zero as the
// position approaches q_soft.  Beliefs follow Glosten & Milgrom (1985): order
// flow imbalance and executed trades move the private mid, and a public
// announcement moves it by the announced log-return.
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
        double lambda_kyle  = 0.015; // ticks of belief per lot of signed flow
        double belief_decay = 0.02;  // pull toward the last executed price
    };

    MarketMakerAS(AgentId id, const std::string& ticker,
                  std::mt19937_64 rng, Params p,
                  RiskLimits rl = {}, LatencyProfile lp = {},
                  InformationProfile ip = {}, EventBus* bus = nullptr);

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "MarketMakerAS"; }

    // Private mid in price ticks — exposed for tests and diagnostics.
    double belief() const { return belief_; }

private:
    Params p_;
    bool   first_tick_ = true;
    double belief_     = 0.0;  // private mid, price ticks
    // Announced log-return not yet folded into the belief.  The bus delivers
    // news inside the news step, before this agent runs on the same tick.
    double pending_news_ = 0.0;
};
