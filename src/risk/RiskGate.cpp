#include "RiskGate.h"
#include "agents/AgentRunner.h"
#include <cmath>
#include <cstdlib>

RiskGate::RiskGate(PositionLedger& ledger, const std::string& ticker)
    : ledger_(ledger), ticker_(ticker)
{}

bool RiskGate::check_position(const RiskLimits& rl, AgentId id, Side side, Qty qty) const {
    Qty current = ledger_.net_qty(id, ticker_);
    Qty projected = current + (side == Side::Buy ? qty : -qty);
    return std::abs(projected) <= rl.max_position;
}

bool RiskGate::check_notional(const RiskLimits& rl, Qty qty, Price price) const {
    return (qty * price) <= rl.max_notional;
}

bool RiskGate::check_drawdown(const RiskLimits& rl, AgentId id, Price mark) const {
    if (rl.max_drawdown_pct >= 1.0) return true;
    Price realized = ledger_.realized_pnl(id, ticker_);
    Price unreal   = ledger_.unrealized_pnl(id, ticker_, mark);
    Price total_pnl = realized + unreal;
    // Reject if loss exceeds max_drawdown_pct of initial notional (approximated from max_notional).
    Price max_loss = static_cast<Price>(rl.max_notional * rl.max_drawdown_pct);
    return total_pnl >= -max_loss;
}

std::vector<FilteredAction> RiskGate::filter(
    const std::vector<AgentAction>& agent_actions, Price mark_price) const
{
    std::vector<FilteredAction> out;
    out.reserve(agent_actions.size());

    for (const auto& aa : agent_actions) {
        IAgent*     agent  = aa.agent;
        const auto& rl     = agent->risk();
        AgentId     aid    = agent->id();

        FilteredAction fa { agent, aa.action, false, false };

        if (const auto* so = std::get_if<SubmitOrder>(&aa.action)) {
            bool pos_ok = check_position(rl, aid, so->side, so->qty);
            bool not_ok = (so->price > 0)
                          ? check_notional(rl, so->qty, so->price)
                          : check_notional(rl, so->qty, mark_price);
            bool dd_ok  = check_drawdown(rl, aid, mark_price);

            // Margin call: equity < margin_req_pct × notional → force flat
            Qty net = ledger_.net_qty(aid, ticker_);
            bool margin_call = false;
            if (rl.margin_req_pct > 0.0 && net != 0 && mark_price > 0) {
                Price equity = ledger_.realized_pnl(aid, ticker_)
                             + ledger_.unrealized_pnl(aid, ticker_, mark_price);
                Price notional_val = std::abs(net) * mark_price;
                margin_call = equity < static_cast<Price>(rl.margin_req_pct * notional_val);
            }

            if (margin_call) {
                // Replace action with zeroing market order
                Side flat_side = (net > 0) ? Side::Sell : Side::Buy;
                fa.action      = SubmitOrder{ flat_side, OrderType::Market, 0,
                                             static_cast<Qty>(std::abs(net)), ticker_ };
                fa.approved    = true;
                fa.margin_call = true;
            } else {
                fa.approved = pos_ok && not_ok && dd_ok;
            }
        } else {
            // CancelOrder / ModifyOrder: always approve (risk checked at submit time).
            fa.approved = true;
        }

        out.push_back(std::move(fa));
    }
    return out;
}
