#pragma once

#include <chrono>
#include <utility>
#include <random>
#include <SupportDefs.h>

static constexpr long TIMESTAMP_BITS  = 39;
static constexpr long MACHINE_ID_BITS = 10;
static constexpr long TIMESTAMP_RESOLUTION = 10;
static constexpr bool MONOTONIC = false;

static constexpr long RANDOM_BITS = 64 - TIMESTAMP_BITS - MACHINE_ID_BITS;

class IceDustGenerator {

public:
    IceDustGenerator();
    IceDustGenerator(uint64 machine_id);

    uint64_t generate();
    uint64_t generate_with_random(uint64 random);

private:
    std::pair<uint64, bool> get_timestamp();

    std::mt19937_64                          *generator;
    std::uniform_int_distribution<uint64>    distrib;

    uint64 machine_id;
    uint64 last_timestamp;
    uint64 last_random;

    uint64 get_random(bool simple_inc);
};
