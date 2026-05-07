#pragma once
// Instantiates the full agent population from PopulationConfig.
// Parameters are sampled deterministically from ParamRange via RngService.
#include "IAgent.h"
#include "core/Config.h"
#include "core/RngService.h"
#include "core/EventBus.h"
#include <memory>
#include <vector>
#include <string>

class AgentFactory {
public:
    AgentFactory(const PopulationConfig& cfg, RngService& rng,
                 const std::string& ticker, EventBus* bus = nullptr);

    // Create all agents and return owning pointers.
    std::vector<std::unique_ptr<IAgent>> create_all();

private:
    const PopulationConfig& cfg_;
    RngService&             rng_;
    std::string             ticker_;
    EventBus*               bus_;
    AgentId                 next_id_ = 1;

    template<typename T>
    T sample(const ParamRange<T>& range, std::mt19937_64& eng);

    AgentId alloc_id() { return next_id_++; }
};
