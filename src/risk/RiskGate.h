#pragma once
// Pre-trade risk filter: validates agent Actions against RiskLimits before routing to book.
#include "RiskLimits.h"
#include "agents/IAgent.h"
#include "agents/Action.h"
#include "ledger/PositionLedger.h"
#include "matching/MatchingEngine.h"
#include <vector>
#include <string>

struct FilteredAction {
    IAgent* agent;
    Action  action;
    bool    approved;
    bool    margin_call;  // true → action was replaced by a zeroing market order
};

class RiskGate {
public:
    explicit RiskGate(PositionLedger& ledger, const std::string& ticker);

    // Filter a batch of agent actions. Approved actions are returned with approved=true.
    // Margin-call actions replace the original with a zeroing market order.
    std::vector<FilteredAction> filter(const std::vector<struct AgentAction>& agent_actions,
                                       Price mark_price) const;

private:
    PositionLedger& ledger_;
    std::string     ticker_;

    bool check_position(const RiskLimits& rl, AgentId id, Side side, Qty qty) const;
    bool check_notional(const RiskLimits& rl, Qty qty, Price price) const;
    bool check_drawdown(const RiskLimits& rl, AgentId id, Price mark) const;
};
