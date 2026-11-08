#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace rkvoice {
constexpr std::uint32_t rate = 16000;

// Explicit little-endian decoding avoids alignment and host byte-order assumptions.
inline int decode16(const unsigned char* p) {
    const int value = static_cast<int>(p[0]) | (static_cast<int>(p[1]) << 8);
    return value >= 32768 ? value - 65536 : value;
}

inline int amplify(int sample, int gain, std::uint64_t& clipped) {
    const int value = sample * gain;  // gain is validated as 1..16.
    if (value < -32768 || value > 32767) ++clipped;
    return std::clamp(value, -32768, 32767);
}

inline void put16(unsigned char* p, std::uint16_t value) {
    p[0] = static_cast<unsigned char>(value & 255U);
    p[1] = static_cast<unsigned char>((value >> 8U) & 255U);
}

inline void put32(unsigned char* p, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i)
        p[i] = static_cast<unsigned char>((value >> (8U * i)) & 255U);
}

inline std::array<unsigned char, 44> wav_header(std::uint32_t data_bytes) {
    std::array<unsigned char, 44> h{};
    std::memcpy(h.data(), "RIFF", 4);
    put32(h.data() + 4, data_bytes + 36);
    std::memcpy(h.data() + 8, "WAVEfmt ", 8);
    put32(h.data() + 16, 16);
    put16(h.data() + 20, 1);  // PCM
    put16(h.data() + 22, 1);  // mono
    put32(h.data() + 24, rate);
    put32(h.data() + 28, rate * 2);
    put16(h.data() + 32, 2);
    put16(h.data() + 34, 16);
    std::memcpy(h.data() + 36, "data", 4);
    put32(h.data() + 40, data_bytes);
    return h;
}

// Own the descriptor; refuse to replace existing recordings.
class OutputFile {
public:
    explicit OutputFile(const char* path) : fd_(::open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) {
        if (fd_ < 0) throw std::runtime_error(std::string("create output: ") + std::strerror(errno));
    }
    ~OutputFile() { if (fd_ >= 0) ::close(fd_); }
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;
    void write(const unsigned char* p, std::size_t n) {
        while (n != 0) {
            const auto done = ::write(fd_, p, n);
            if (done < 0 && errno == EINTR) continue;
            if (done <= 0) throw std::runtime_error("output write failed");
            const auto count = static_cast<std::size_t>(done);
            p += count;
            n -= count;
        }
    }
    void finish() {
        const int fd = fd_;
        fd_ = -1;
        if (::close(fd) != 0) throw std::runtime_error("output close failed");
    }
private:
    int fd_;
};

inline int number(const char* text, int minimum, int maximum) {
    const std::string s(text);
    std::size_t used = 0;
    const int value = std::stoi(s, &used);
    if (used != s.size() || value < minimum || value > maximum)
        throw std::runtime_error("numeric argument out of range");
    return value;
}

inline void self_test() {
    const unsigned char low[] = {0, 128};
    const unsigned char high[] = {255, 127};
    const unsigned char minus_one[] = {255, 255};
    std::uint64_t clipped = 0;
    if (decode16(low) != -32768 || decode16(high) != 32767 || decode16(minus_one) != -1 ||
        amplify(-32768, 1, clipped) != -32768 || amplify(32767, 1, clipped) != 32767 ||
        clipped != 0 || amplify(-10000, 4, clipped) != -32768 ||
        amplify(10000, 4, clipped) != 32767 || clipped != 2 ||
        amplify(-123, 4, clipped) != -492)
        throw std::runtime_error("PCM boundary test failed");
    const auto h = wav_header(32000);
    if (h[22] != 1 || h[24] != 128 || h[25] != 62 || h[40] != 0 || h[41] != 125)
        throw std::runtime_error("WAV header test failed");
    std::cout << "PCM_SELF_TEST_OK sizeof(void*)=" << sizeof(void*) << '\n';
}


} // namespace rkvoice
