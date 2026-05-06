#pragma once
#include "core/Types.h"

// Common interface for all fundamental value processes.
// step() advances the process by one time unit of size dt (in whatever unit the caller uses).
class IFundamentalValueProcess {
public:
    virtual ~IFundamentalValueProcess() = default;

    // Advance one time step.
    virtual void step(Tick now, double dt) = 0;

    // Current fundamental value (in the same double units as s0).
    virtual double current_value() const = 0;

    // Instantaneous drift (μ or κ(θ−S), etc.) at current state.
    virtual double current_drift() const = 0;

    // Instantaneous volatility (σ) at current state.
    virtual double current_vol() const = 0;

    virtual const char* name() const = 0;
};
