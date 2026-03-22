#include "uuid.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace agui {

// Initialize static member
std::atomic<uint32_t> UuidGenerator::m_counter(0);

// Global random number generator
static std::mt19937& getGenerator() {
    thread_local std::mt19937 generator(std::random_device{}());
    return generator;
}

uint64_t UuidGenerator::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return static_cast<uint64_t>(millis);
}

uint32_t UuidGenerator::getRandomNumber() {
    std::uniform_int_distribution<uint32_t> distribution(0, 0xFFFFFFFF);
    return distribution(getGenerator());
}

std::string UuidGenerator::generate() {
    uint64_t timestamp = getTimestamp();
    uint32_t random = getRandomNumber();
    uint32_t count = m_counter.fetch_add(1);

    // Custom hybrid UUID formatted to resemble UUID v4 layout:
    //   [32-bit timestamp low] - [16-bit timestamp high] - 4[12-bit random] - [16-bit variant+random] - [32-bit counter][16-bit random]
    // NOTE: This is NOT a standards-compliant UUID v4; the first 48 bits encode a
    // millisecond timestamp rather than random data. The '4' version nibble and
    // variant bits are kept for visual compatibility with UUID-shaped identifiers.

    char uuid[37];
    snprintf(uuid, sizeof(uuid), "%08x-%04x-4%03x-%04x-%08x%04x",
             static_cast<uint32_t>(timestamp & 0xFFFFFFFF),
             static_cast<uint16_t>((timestamp >> 32) & 0xFFFF),
             static_cast<uint16_t>(random & 0x0FFF),
             static_cast<uint16_t>(0x8000 | ((random >> 12) & 0x3FFF)),
             count,
             static_cast<uint32_t>(random >> 16)
    );

    return std::string(uuid);
}

}  // namespace agui
