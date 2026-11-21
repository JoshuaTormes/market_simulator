#pragma once
#include "core/Types.h"
#include <string>

// A market-moving news event emitted by INewsEventProcess and delivered via EventBus.
// Impact is a log-return (e.g. +0.02 = +2% instantaneous shock to fundamental).
// Agents may interpret impact with heterogeneous noise controlled by dispersion_sigma.
struct NewsEvent {
    Tick        announce_tick  = 0;
    std::string ticker;
    double      impact_log_return    = 0.0; // signed log-return shock (Student-t drawn)
    double      duration_ticks       = 0.0; // how many ticks the effect persists
    double      dispersion_sigma     = 0.0; // heterogeneous interpretation: agent sees
                                            // impact · (1 + N(0, dispersion_sigma))
};
