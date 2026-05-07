#pragma once
#include "IAgent.h"
#include "core/RngService.h"
#include <random>
#include <string>

// Minimal base class: stores identity, ticker, RNG, and policy objects.
// Agents only read the snapshot they receive; no ledger access here.
class AgentBase : public IAgent {
public:
    AgentBase(AgentId id, std::string ticker,
              std::mt19937_64 rng,
              RiskLimits rl,
              LatencyProfile lp,
              InformationProfile ip);

    AgentId            id()      const override { return id_; }
    RiskLimits         risk()    const override { return rl_; }
    LatencyProfile     latency() const override { return lp_; }
    InformationProfile info()    const override { return ip_; }

protected:
    // Helper: build a SubmitOrder wrapped in Action.
    Action submit(Side side, OrderType type, Price price, Qty qty, Tick ttl = 0) const;

    const AgentId          id_;
    const std::string      ticker_;
    std::mt19937_64        rng_;
    const RiskLimits       rl_;
    const LatencyProfile   lp_;
    const InformationProfile ip_;
};
