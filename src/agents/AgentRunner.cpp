#include "AgentRunner.h"
#include "core/RngService.h"
#include <random>

AgentRunner::AgentRunner(std::vector<IAgent*> agents)
    : agents_(std::move(agents))
{}

std::vector<AgentAction> AgentRunner::run(const MarketSnapshot& snap,
                                          double fundamental_value) const {
    std::vector<AgentAction> out;
    // Static RNG seeded once; determinism provided by per-agent RNG inside each agent.
    static std::mt19937_64 derive_rng(0xDEADBEEF12345678ULL);

    for (IAgent* agent : agents_) {
        AgentSnapshot as = AgentSnapshot::derive_from(snap, agent->info(), derive_rng);
        if (agent->info().sees_fundamental) {
            as.fundamental_value = fundamental_value;
            as.has_fundamental   = true;
        }

        auto actions = agent->on_market_data(as);
        for (auto& act : actions)
            out.push_back({ agent, std::move(act) });
    }
    return out;
}
