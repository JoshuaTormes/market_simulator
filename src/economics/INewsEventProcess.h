#pragma once
#include "core/Types.h"
#include <vector>
#include "NewsEvent.h"

// Interface for news event generators.
// step() returns all events that arrive during this tick (may be empty).
class INewsEventProcess {
public:
    virtual ~INewsEventProcess() = default;

    // Advance one tick and return any events that occurred.
    virtual std::vector<NewsEvent> step(Tick now) = 0;

    virtual const char* name() const = 0;
};
