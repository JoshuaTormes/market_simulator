#include "AgentRunner.h"
#include <random>

AgentRunner::AgentRunner(std::vector<IAgent*> agents, uint64_t seed,
                         PositionLedger* ledger, std::string ticker)
    : agents_(std::move(agents)), seed_(seed), ledger_(ledger), ticker_(std::move(ticker))
{}

std::vector<AgentAction> AgentRunner::run(const MarketSnapshot& snap,
                                          double fundamental_value,
                                          Tick now) {
    // Push current snapshot into history for info_delay_ticks support.
    history_.push_back(snap);
    if (history_.size() > kMaxDelay + 1)
        history_.pop_front();

    std::vector<AgentAction> out;

    for (IAgent* agent : agents_) {
        // Per-agent per-tick noise RNG via splitmix64 finalizer (deterministic, no static state).
        uint64_t h = seed_ ^ ((uint64_t)agent->id() * 0x9e3779b97f4a7c15ULL)
                           ^ ((uint64_t)now         * 0x6c62272e07bb0142ULL);
        h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
        h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
        h = h ^ (h >> 31);
        std::mt19937_64 noise_rng(h);

        // Honour info_delay_ticks: supply historical snapshot if available.
        Tick delay = agent->info().info_delay_ticks;
        const MarketSnapshot& agent_snap =
            (delay > 0 && static_cast<Tick>(history_.size()) > delay)
            ? history_[history_.size() - 1 - delay]
            : snap;

        AgentSnapshot as = AgentSnapshot::derive_from(agent_snap, agent->info(), noise_rng);
        if (agent->info().sees_fundamental) {
            as.fundamental_value = fundamental_value;
            as.has_fundamental   = true;
        }
        if (ledger_ && !ticker_.empty())
            as.own_inventory = static_cast<double>(ledger_->net_qty(agent->id(), ticker_));

        auto actions = agent->on_market_data(as);
        for (auto& act : actions)
            out.push_back({ agent, std::move(act) });
    }
    return out;
}
