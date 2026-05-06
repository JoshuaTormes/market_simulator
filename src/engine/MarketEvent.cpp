#include "MarketEvent.h"

MarketEvent::MarketEvent(
    const std::string& ticker,
    double impact,
    uint64_t startTick,
    uint64_t duration
)
    : ticker(ticker),
      impact(impact),
      startTick(startTick),
      duration(duration) {}

bool MarketEvent::isActive(uint64_t tick) const {
    return tick >= startTick && tick < startTick + duration;
}

const std::string& MarketEvent::getTicker() const {
    return ticker;
}

double MarketEvent::getImpact() const {
    return impact;
}

uint64_t MarketEvent::getStartTick() const {
    return startTick;
}

uint64_t MarketEvent::getDuration() const {
    return duration;
}
