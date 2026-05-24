#pragma once
// Dispatches AgentSnapshot to each agent and collects their Actions.
#include "IAgent.h"
#include "marketdata/AgentSnapshot.h"
#include <deque>
#include <vector>

struct AgentAction {
    IAgent* agent;
    Action  action;
};

class AgentRunner {
public:
    // seed: used to derive per-agent per-tick noise RNG deterministically.
    AgentRunner(std::vector<IAgent*> agents, uint64_t seed);

    // Call each agent with its (possibly delayed) snapshot, return all (agent, action) pairs.
    // now: current tick — used for per-tick noise seeding and info_delay_ticks lookup.
    std::vector<AgentAction> run(const MarketSnapshot& snap,
                                 double fundamental_value,
                                 Tick now);

private:
    std::vector<IAgent*> agents_;
    uint64_t seed_;

    // Snapshot history for info_delay_ticks (max kMaxDelay ticks of history).
    static constexpr Tick kMaxDelay = 20;
    std::deque<MarketSnapshot> history_;
};
