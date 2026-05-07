#pragma once
// Dispatches AgentSnapshot to each agent and collects their Actions.
#include "IAgent.h"
#include "marketdata/AgentSnapshot.h"
#include <vector>

struct AgentAction {
    IAgent* agent;
    Action  action;
};

class AgentRunner {
public:
    explicit AgentRunner(std::vector<IAgent*> agents);

    // Call each agent with its snapshot, return all (agent, action) pairs.
    // Each agent gets its own snapshot (different info profiles → different perceptions).
    std::vector<AgentAction> run(const MarketSnapshot& snap, double fundamental_value) const;

private:
    std::vector<IAgent*> agents_;
};
