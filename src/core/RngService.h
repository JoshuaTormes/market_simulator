#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <functional>

// Centralised RNG — single global seed; each consumer gets a deterministic
// mt19937_64 derived via splitmix64(hash(seed, consumer_id)).
// Never use std::random_device anywhere else in the codebase.

class RngService {
public:
    explicit RngService(uint64_t seed) : seed_(seed) {}

    // Returns a fully seeded mt19937_64 for the given consumer.
    // Calling this twice with the same id gives the same engine state.
    std::mt19937_64 for_consumer(const std::string& consumer_id) const;

    uint64_t global_seed() const { return seed_; }

private:
    uint64_t seed_;

    // Splitmix64 — fast, well-distributed hash mixer (Steele et al. 2014).
    static uint64_t splitmix64(uint64_t x);
};
