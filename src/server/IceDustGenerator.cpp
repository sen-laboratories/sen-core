/* TSID generator used for generating compact, efficient and reasonably unique ID's used as SEN:ID attributes
 * reference: https://www.foxhound.systems/blog/time-sorted-unique-identifiers/
 * taken from https://github.com/lynzrand/icedust/blob/master/src/lib.rs
 * converted using https://www.codeconvert.ai/app
 */
#include <chrono>
#include <utility>
//#include <random>

template <typename G, uint8_t TIMESTAMP_BITS, uint64_t TIMESTAMP_RESOLUTION, uint8_t MACHINE_ID_BITS, bool MONOTONIC>
class IceDustGenerator {
public:
    IceDustGenerator(G generator, uint64_t machine_id, std::chrono::system_clock::time_point epoch)
        : generator(generator), machine_id(machine_id), epoch(epoch), last_timestamp(0), last_random(0) {
        static_assert((TIMESTAMP_BITS + MACHINE_ID_BITS) < 64, "TIMESTAMP_BITS + MACHINE_ID_BITS must be less than 64");
        machine_id = machine_id & (std::numeric_limits<uint64_t>::max() >> (64 - MACHINE_ID_BITS));
    }

    uint64_t generate() {
        auto [timestamp, inc] = get_timestamp();
        bool simple_inc = inc && MONOTONIC;
        uint64_t random = get_random(simple_inc);
        uint64_t res = (timestamp << (MACHINE_ID_BITS + RANDOM_BITS)) | (machine_id << RANDOM_BITS) | random;
        return res;
    }

    uint64_t generate_with_random(uint64_t random) {
        auto [timestamp, _] = get_timestamp();
        uint64_t res = (timestamp << (MACHINE_ID_BITS + RANDOM_BITS)) | (machine_id << RANDOM_BITS) | random;
        return res;
    }

private:
    static constexpr uint8_t RANDOM_BITS = 64 - TIMESTAMP_BITS - MACHINE_ID_BITS;

    G generator;
    uint64_t machine_id;
    std::chrono::system_clock::time_point epoch;
    uint64_t last_timestamp;
    uint64_t last_random;

    uint64_t get_random(bool simple_inc) {
        uint64_t random;
        if (simple_inc) {
            last_random = last_random + 1;
            random = last_random;
        } else {
            random = generator();
            last_random = random;
        }
        random = random & (std::numeric_limits<uint64_t>::max() >> (64 - RANDOM_BITS));
        return random;
    }

    std::pair<uint64_t, bool> get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto duration = now - epoch;
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        if (timestamp >= ((1 << TIMESTAMP_BITS) * TIMESTAMP_RESOLUTION)) {
            return {0, false};
        }
        timestamp = timestamp / TIMESTAMP_RESOLUTION;
        uint64_t last = last_timestamp;
        if (last > timestamp) {
            return {0, false};
        }
        last_timestamp = timestamp;
        return {timestamp, timestamp == last};
    }
};

template <typename G, uint8_t TIMESTAMP_BITS, uint64_t TIMESTAMP_RESOLUTION, bool MONOTONIC>
class IceDustGenerator<G, TIMESTAMP_BITS, TIMESTAMP_RESOLUTION, 0, MONOTONIC> {
public:
    IceDustGenerator(G generator)
        : generator(generator), machine_id(0), epoch(std::chrono::system_clock::from_time_t(0)), last_timestamp(0), last_random(0) {
        static_assert(TIMESTAMP_BITS < 64, "TIMESTAMP_BITS must be less than 64");
    }

private:
    G generator;
    uint64_t machine_id;
    std::chrono::system_clock::time_point epoch;
    uint64_t last_timestamp;
    uint64_t last_random;
    static constexpr uint8_t RANDOM_BITS = 64 - TIMESTAMP_BITS - 0; // no machine ID

    uint64_t get_random(bool simple_inc) {
        uint64_t random;
        if (simple_inc) {
            last_random = last_random + 1;
            random = last_random;
        } else {
            random = generator();
            last_random = random;
        }
        random = random & (std::numeric_limits<uint64_t>::max() >> (64 - RANDOM_BITS));
        return random;
    }

    std::pair<uint64_t, bool> get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto duration = now - epoch;
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        if (timestamp >= ((1 << TIMESTAMP_BITS) * TIMESTAMP_RESOLUTION)) {
            return {0, false};
        }
        timestamp = timestamp / TIMESTAMP_RESOLUTION;
        uint64_t last = last_timestamp;
        if (last > timestamp) {
            return {0, false};
        }
        last_timestamp = timestamp;
        return {timestamp, timestamp == last};
    }
};

template <typename G>
IceDustGenerator<G, 39, 10, 0, true> new_default(G generator) {
    return IceDustGenerator<G, 39, 10, 0, true>(generator);
}

template <typename G, uint8_t TIMESTAMP_BITS, uint64_t TIMESTAMP_RESOLUTION, uint8_t MACHINE_ID_BITS, bool MONOTONIC>
IceDustGenerator<G, TIMESTAMP_BITS, TIMESTAMP_RESOLUTION, MACHINE_ID_BITS, MONOTONIC> new_generator(G generator, uint64_t machine_id, std::chrono::system_clock::time_point epoch) {
    return IceDustGenerator<G, TIMESTAMP_BITS, TIMESTAMP_RESOLUTION, MACHINE_ID_BITS, MONOTONIC>(generator, machine_id, epoch);
}

