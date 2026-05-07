#include "AgentBase.h"

AgentBase::AgentBase(AgentId id, std::string ticker,
                     std::mt19937_64 rng,
                     RiskLimits rl,
                     LatencyProfile lp,
                     InformationProfile ip)
    : id_(id)
    , ticker_(std::move(ticker))
    , rng_(std::move(rng))
    , rl_(rl)
    , lp_(lp)
    , ip_(ip)
{}

Action AgentBase::submit(Side side, OrderType type, Price price, Qty qty, Tick ttl) const {
    return SubmitOrder{ side, type, price, qty, ticker_, ttl };
}
