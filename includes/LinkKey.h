#pragma once

#include <array>
#include <cstdint>
#include <string>

class LinkKey {
public:
    static constexpr std::size_t SIZE = 16;   // a Link Key is 16 bytes

    explicit LinkKey(const std::array<std::uint8_t, SIZE>& bytes);

    static LinkKey fromHex(const std::string& hex);   // "A73F..." → LinkKey

    std::string toHex() const;      // LinkKey → "A73F..."
    LinkKey reversed() const;       // byte-order flip

    const std::array<std::uint8_t, SIZE>& bytes() const { return bytes_; }

private:
    std::array<std::uint8_t, SIZE> bytes_;
};
