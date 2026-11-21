#pragma once
#include "Action.h"
#include "core/LatencyProfile.h"
#include "marketdata/AgentSnapshot.h"
#include "marketdata/InformationProfile.h"
#include "risk/RiskLimits.h"
#include <vector>

// Pure interface for all simulation agents.
// Agents are stateful objects: they observe AgentSnapshot each tick and emit Actions.
class IAgent {
public:
    virtual ~IAgent() = default;

    // Called once per tick. Returns zero or more actions.
    virtual std::vector<Action> on_market_data(const AgentSnapshot& snap) = 0;

    // Static agent properties (set at construction, do not change mid-sim).
    virtual LatencyProfile     latency() const = 0;
    virtual RiskLimits         risk()    const = 0;
    virtual InformationProfile info()    const = 0;
    virtual const char*        type()    const = 0;
    virtual AgentId            id()      const = 0;
};
