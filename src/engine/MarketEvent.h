#pragma once
#include <string>
#include <cstdint>

class MarketEvent {
public:
    MarketEvent(
        const std::string& ticker,
        double impact,
        uint64_t startTick,
        uint64_t duration
    );

    bool isActive(uint64_t tick) const;

    const std::string& getTicker() const;
    double getImpact() const;
    uint64_t getStartTick() const;
    uint64_t getDuration() const;

private:
    std::string ticker;
    double impact;       // percentual que afeta percepção do agente (-0.05 = -5%)
    uint64_t startTick;
    uint64_t duration;   // duração em ticks
};
