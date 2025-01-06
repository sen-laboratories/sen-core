/* TSID generator used for generating compact, efficient and reasonably unique ID's used as SEN:ID attributes
 * reference: https://www.foxhound.systems/blog/time-sorted-unique-identifiers/
 * taken from https://github.com/lynzrand/icedust/blob/master/src/lib.rs
 * converted using https://www.codeconvert.ai/app
 */
#include "IceDustGenerator.h"
#include <String.h>

IceDustGenerator::IceDustGenerator()
: IceDustGenerator(BString::HashValue("hokusai-machine") << 2 | BString::HashValue("sen-labs-server"))
{
}

IceDustGenerator::IceDustGenerator(uint64 machine_id)
: machine_id(machine_id), last_timestamp(0), last_random(0)
{
    static_assert((TIMESTAMP_BITS + MACHINE_ID_BITS) < 64, "TIMESTAMP_BITS + MACHINE_ID_BITS must be less than 64");
    machine_id = machine_id & (UINT64_MAX >> (64 - MACHINE_ID_BITS));

    // using newer C++11 random number generator
    // see https://en.cppreference.com/w/cpp/numeric/random
    // Seed with a real random value, if available
    std::random_device rd;
    generator = new std::mt19937_64(rd());
    distrib   = std::uniform_int_distribution<uint64>(0, UINT64_MAX);
}

uint64 IceDustGenerator::generate() {
    auto [timestamp, inc] = get_timestamp();
    bool simple_inc = inc && MONOTONIC;
    uint64 random = get_random(simple_inc);
    uint64 res = (timestamp << (MACHINE_ID_BITS + RANDOM_BITS)) | (machine_id << RANDOM_BITS) | random;
    return res;
}

uint64 IceDustGenerator::generate_with_random(uint64 random) {
    auto [timestamp, _] = get_timestamp();
    uint64 res = (timestamp << (MACHINE_ID_BITS + RANDOM_BITS)) | (machine_id << RANDOM_BITS) | random;
    return res;
}

uint64 IceDustGenerator::get_random(bool simple_inc) {
    uint64 random;
    if (simple_inc) {
        last_random = last_random + 1;
        random = last_random;
    } else {
        random = distrib(*generator);
        last_random = random;
    }
    random = random & (UINT64_MAX >> (64 - RANDOM_BITS));
    return random;
}

std::pair<uint64_t, bool> IceDustGenerator::get_timestamp() {
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    if (timestamp >= ((1L << TIMESTAMP_BITS) * TIMESTAMP_RESOLUTION)) {
        return {0, false};
    }
    timestamp = timestamp / TIMESTAMP_RESOLUTION;
    uint64 last = last_timestamp;
    if (last > timestamp) {
        return {0, false};
    }
    last_timestamp = timestamp;
    return {timestamp, timestamp == last};
}

