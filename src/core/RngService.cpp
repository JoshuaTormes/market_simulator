#include "RngService.h"

// splitmix64 by Sebastiano Vigna — https://prng.di.unimi.it/splitmix64.c
uint64_t RngService::splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

std::mt19937_64 RngService::for_consumer(const std::string& consumer_id) const {
    uint64_t h = std::hash<std::string>{}(consumer_id);
    uint64_t s0 = splitmix64(seed_ ^ h);
    uint64_t s1 = splitmix64(s0);
    uint64_t s2 = splitmix64(s1);
    uint64_t s3 = splitmix64(s2);

    std::seed_seq seq{
        static_cast<uint32_t>(s0 >> 32), static_cast<uint32_t>(s0),
        static_cast<uint32_t>(s1 >> 32), static_cast<uint32_t>(s1),
        static_cast<uint32_t>(s2 >> 32), static_cast<uint32_t>(s2),
        static_cast<uint32_t>(s3 >> 32), static_cast<uint32_t>(s3)
    };
    return std::mt19937_64(seq);
}
