#include "LinkKey.h"
#include <algorithm>    // std::reverse
#include <charconv>     // std::from_chars
#include <stdexcept>    // std::invalid_argument
#include <cstdio>       // std::snprintf (for toHex)

// 1. Constructor — just copy the bytes in.
LinkKey::LinkKey(const std::array<std::uint8_t, SIZE>& bytes)
    : bytes_(bytes)
{}

// 2. fromHex — parse a 32-char hex string into 16 bytes.
LinkKey LinkKey::fromHex(const std::string& hex) {
    if (hex.size() != SIZE * 2) {                       // 16 bytes = 32 hex chars
        throw std::invalid_argument("hex key must be 32 characters");
    }

    std::array<std::uint8_t, SIZE> bytes{};
    for (std::size_t i = 0; i < SIZE; ++i) {
        const char* start = hex.data() + i * 2;         // each byte = 2 chars
        const char* end   = start + 2;
        auto [ptr, ec] = std::from_chars(start, end, bytes[i], 16);  // base 16
        if (ec != std::errc{} || ptr != end) {
            throw std::invalid_argument("invalid hex characters");
        }
    }
    return LinkKey(bytes);
}

// 3. toHex — turn the 16 bytes into a 32-char uppercase hex string.
std::string LinkKey::toHex() const {
    std::string out;
    out.reserve(SIZE * 2);
    char buf[3];                                         // 2 hex digits + '\0'
    for (std::uint8_t b : bytes_) {
        std::snprintf(buf, sizeof(buf), "%02X", b);
        out += buf;
    }
    return out;
}

// 4. reversed — return a new LinkKey with the byte order flipped.
LinkKey LinkKey::reversed() const {
    std::array<std::uint8_t, SIZE> copy = bytes_;
    std::reverse(copy.begin(), copy.end());
    return LinkKey(copy);
}
