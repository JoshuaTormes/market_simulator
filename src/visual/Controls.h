#pragma once
// Simulation controls: pause/resume, step(N), speed slider, inject-news modal.
#include "sim/SimulationLoop.h"
#include "core/EventBus.h"
#include "economics/NewsEvent.h"

class Controls {
public:
    Controls(SimulationLoop& sim, EventBus& bus)
        : sim_(sim), bus_(bus) {
        // Push the default down to the loop: the slider only pushes on change,
        // so without this the run starts unthrottled and is over instantly.
        sim_.set_tick_delay_us(static_cast<uint64_t>(delay_us_));
    }

    void draw();

private:
    SimulationLoop& sim_;
    EventBus&       bus_;

    int    step_n_     = 10;
    // 200 µs per tick (~5k ticks/s): fast enough to watch the book breathe,
    // slow enough that the run does not finish before the first frame is drawn.
    int    delay_us_   = 200;

    // Inject-news modal state
    float  news_impact_    = 0.01f;
    float  news_duration_  = 20.0f;
    float  news_disp_      = 0.3f;
};
