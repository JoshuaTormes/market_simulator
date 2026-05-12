#pragma once
// Simulation controls: pause/resume, step(N), speed slider, inject-news modal.
#include "sim/SimulationLoop.h"
#include "core/EventBus.h"
#include "economics/NewsEvent.h"

class Controls {
public:
    Controls(SimulationLoop& sim, EventBus& bus)
        : sim_(sim), bus_(bus) {}

    void draw();

private:
    SimulationLoop& sim_;
    EventBus&       bus_;

    int    step_n_     = 10;
    int    delay_us_   = 0;

    // Inject-news modal state
    float  news_impact_    = 0.01f;
    float  news_duration_  = 20.0f;
    float  news_disp_      = 0.3f;
};
