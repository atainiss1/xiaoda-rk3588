#pragma once
#include "pcm.hpp"
#include <pulse/error.h>
#include <pulse/simple.h>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <vector>
#include <atomic>

namespace rkvoice::audio {
using namespace rkvoice;
volatile std::sig_atomic_t stopped = 0;
inline std::atomic<bool> session_cancelled{false};
inline std::atomic<bool> turn_interrupted{false};
inline bool session_stopping() { return stopped || session_cancelled.load(); }
inline bool cancelled() { return session_stopping() || turn_interrupted.load(); }
void on_stop(int) { stopped = 1; }
void on_timeout(int) {
    constexpr char message[] = "rkvoice_audio: audio operation timed out\n";
    const auto ignored = ::write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)ignored;
    ::_exit(124);
}
// The simple API blocks. Bound each call so a disconnected audio server cannot
// leave this sequential CLI hanging forever. No worker threads in this version.
struct Deadline {
    Deadline() { ::alarm(20); }
    ~Deadline() { ::alarm(0); }
};
using Stream = std::unique_ptr<pa_simple, decltype(&pa_simple_free)>;
const char* device(bool capture) {
    const char* configured = std::getenv(capture ? "RKVOICE_SOURCE" : "RKVOICE_SINK");
    if (configured && *configured) return configured;
    return capture ? "alsa_input.platform-es8388-sound.HiFi__hw_rockchipes8388__source"
                   : "alsa_output.platform-es8388-sound.HiFi__hw_rockchipes8388__sink";
}
Stream connect(bool capture, std::uint32_t sample_rate, std::uint8_t channels) {
    const pa_sample_spec spec{PA_SAMPLE_S16LE, sample_rate, channels};
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<std::uint32_t>(-1);
    attr.tlength = sample_rate * channels * 2 / 5;  // playback target: 200 ms
    attr.prebuf = static_cast<std::uint32_t>(-1);
    attr.minreq = static_cast<std::uint32_t>(-1);
    attr.fragsize = sample_rate * channels * 2 / 10;  // capture: 100 ms
    int error = 0;
    Deadline deadline;
    Stream stream(pa_simple_new(nullptr, "rk3588-voice-cpp",
                  capture ? PA_STREAM_RECORD : PA_STREAM_PLAYBACK, device(capture),
                  capture ? "microphone" : "speaker", &spec, nullptr, &attr, &error), pa_simple_free);
    if (!stream) throw std::runtime_error(std::string("PulseAudio connect: ") + pa_strerror(error));
    return stream;
}
void read_audio(pa_simple* stream, unsigned char* data, std::size_t size) {
    int error = 0;
    Deadline deadline;
    if (pa_simple_read(stream, data, size, &error) < 0)
        throw std::runtime_error(std::string("capture: ") + pa_strerror(error));
}
void play_pcm(const std::vector<unsigned char>& pcm, std::uint32_t sample_rate, std::uint8_t channels) {
    auto stream = connect(false, sample_rate, channels);
    int error = 0;
    const std::size_t block = static_cast<std::size_t>(sample_rate / 100) * channels * 2;
    const std::size_t frames = pcm.size() / (channels * 2U);
    const std::size_t fade_frames = std::max<std::size_t>(1, sample_rate / 100);
    std::vector<unsigned char> output(block, 0);
    auto write_block = [&](const unsigned char* data, std::size_t size) {
        Deadline deadline;
        if (pa_simple_write(stream.get(), data, size, &error) < 0)
            throw std::runtime_error(std::string("playback: ") + pa_strerror(error));
    };
    // Let the playback path settle before speech; soften non-zero boundaries.
    for (int i = 0; i < 30 && !stopped; ++i) write_block(output.data(), output.size());
    for (std::size_t offset = 0; offset < pcm.size() && !stopped; offset += block) {
        const auto count = std::min(block, pcm.size() - offset);
        for (std::size_t i = 0; i < count; i += 2) {
            const auto frame = (offset + i) / (channels * 2U);
            const auto ramp = std::min({fade_frames, frame, frames - 1 - frame});
            const auto value = static_cast<int>(static_cast<long long>(decode16(pcm.data() + offset + i)) *
                                               static_cast<long long>(ramp) / static_cast<long long>(fade_frames));
            put16(output.data() + i, static_cast<std::uint16_t>(value));
        }
        write_block(output.data(), count);
    }
    std::fill(output.begin(), output.end(), 0);
    for (int i = 0; i < 10 && !stopped; ++i) write_block(output.data(), output.size());
    Deadline deadline;
    const int status = stopped ? pa_simple_flush(stream.get(), &error) : pa_simple_drain(stream.get(), &error);
    if (status < 0) throw std::runtime_error(std::string("finish playback: ") + pa_strerror(error));
}

std::uint16_t get16(const unsigned char* p) {
    return static_cast<std::uint16_t>(static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8U));
}
std::uint32_t get32(const unsigned char* p) {
    std::uint32_t value = 0;
    for (unsigned int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(p[i]) << (8U * i);
    return value;
}
struct Wave {
    std::uint32_t sample_rate = 0;
    std::uint8_t channels = 0;
    std::vector<unsigned char> pcm;
};
Wave load_wave(const char* path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open WAV");
    const auto length = in.tellg();
    if (length < 12 || length > 64 * 1024 * 1024) throw std::runtime_error("WAV must be 12 bytes..64 MiB");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4))
        throw std::runtime_error("invalid RIFF/WAVE file");
    const std::uint64_t end64 = static_cast<std::uint64_t>(get32(bytes.data() + 4)) + 8;
    if (end64 != bytes.size()) throw std::runtime_error("RIFF size mismatch");
    const auto end = bytes.size();
    Wave result;
    bool have_format = false, have_data = false;
    std::size_t data_offset = 0, data_size = 0;
    for (std::size_t offset = 12; offset < end;) {
        if (end - offset < 8) throw std::runtime_error("truncated WAV chunk header");
        const auto size = static_cast<std::size_t>(get32(bytes.data() + offset + 4));
        const auto payload = offset + 8;
        if (size > end - payload) throw std::runtime_error("truncated WAV chunk");
        const auto* p = bytes.data() + payload;
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0) {
            if (have_format || size < 16 || get16(p) != 1 || get16(p + 14) != 16)
                throw std::runtime_error("only one PCM16 fmt chunk is supported");
            const auto channels = get16(p + 2);
            result.sample_rate = get32(p + 4);
            if ((channels != 1 && channels != 2) || result.sample_rate < 8000 || result.sample_rate > 96000 ||
                get16(p + 12) != channels * 2 || get32(p + 8) != result.sample_rate * channels * 2)
                throw std::runtime_error("unsupported or inconsistent WAV format");
            result.channels = static_cast<std::uint8_t>(channels);
            have_format = true;
        } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
            if (have_data) throw std::runtime_error("multiple data chunks are unsupported");
            data_offset = payload;
            data_size = size;
            have_data = true;
        }
        const auto padded = size + (size & 1U);
        if (padded > end - payload) throw std::runtime_error("missing WAV padding");
        offset = payload + padded;
    }
    if (!have_format || !have_data || data_size == 0 || data_size % (result.channels * 2U) != 0)
        throw std::runtime_error("missing format/data or incomplete PCM frames");
    result.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                      bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + data_size));
    return result;
}
void inspect(const Wave& wave) {
    int peak = 0;
    for (std::size_t i = 0; i < wave.pcm.size(); i += 2)
        peak = std::max(peak, std::abs(decode16(wave.pcm.data() + i)));
    std::cout << "WAV rate=" << wave.sample_rate << " channels=" << static_cast<int>(wave.channels)
              << " frames=" << wave.pcm.size() / (wave.channels * 2U) << " peak=" << peak << std::endl;
}
void play(const char* path) {
    const auto wave = load_wave(path);
    inspect(wave);
    play_pcm(wave.pcm, wave.sample_rate, wave.channels);
    std::cout << (stopped ? "PLAYBACK_INTERRUPTED" : "PLAYBACK_DONE") << std::endl;
}
void tone() {
    std::vector<unsigned char> pcm(rate * 2 / 3);
    for (std::size_t i = 0; i < pcm.size() / 2; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double envelope = std::min({1.0, t / 0.02, (1.0 / 3.0 - t) / 0.02});
        const int value = static_cast<int>(6000 * envelope * std::sin(2 * 3.141592653589793 * 880 * t));
        put16(pcm.data() + i * 2, static_cast<std::uint16_t>(value));
    }
    play_pcm(pcm, rate, 1);
}
void record(const char* path, int seconds, int gain) {
    OutputFile out(path);
    const auto started = std::chrono::steady_clock::now();
    std::cout << "PREPARING_CAPTURE: wait for RECORD_READY before speaking" << std::endl;
    auto stream = connect(true, rate, 2);
    std::array<unsigned char, 640> stereo{};  // 160 stereo frames, 10 ms
    for (int i = 0; i < 50 && !stopped; ++i) read_audio(stream.get(), stereo.data(), stereo.size());
    const auto ready_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "RECORD_READY seconds=" << seconds << " gain=" << gain
              << " preparation_ms=" << ready_ms << std::endl;
    std::vector<unsigned char> mono;
    mono.reserve(static_cast<std::size_t>(seconds) * rate * 2);
    double square_sum = 0;
    int input_peak = 0, output_peak = 0;
    std::uint64_t clipped = 0;
    for (int block = 0; block < seconds * 100 && !stopped; ++block) {
        read_audio(stream.get(), stereo.data(), stereo.size());
        for (std::size_t i = 0; i < stereo.size(); i += 4) {
            const int left = decode16(stereo.data() + i);
            const int value = amplify(left, gain, clipped);
            input_peak = std::max(input_peak, std::abs(left));
            output_peak = std::max(output_peak, std::abs(value));
            square_sum += static_cast<double>(left) * left;
            const auto encoded = static_cast<std::uint16_t>(value);
            mono.push_back(static_cast<unsigned char>(encoded & 255U));
            mono.push_back(static_cast<unsigned char>(encoded >> 8U));
        }
    }
    stream.reset();  // release capture before starting playback
    if (mono.empty()) throw std::runtime_error("no captured audio; empty output file reserved");
    const auto header = wav_header(static_cast<std::uint32_t>(mono.size()));
    out.write(header.data(), header.size());
    out.write(mono.data(), mono.size());
    out.finish();
    const auto frames = mono.size() / 2;
    std::cout << (stopped ? "RECORD_INTERRUPTED" : "RECORD_DONE") << " frames=" << frames
              << " seconds=" << static_cast<double>(frames) / rate
              << " input_rms=" << std::sqrt(square_sum / static_cast<double>(frames))
              << " input_peak=" << input_peak << " output_peak=" << output_peak
              << " clipped_samples=" << clipped << std::endl;
}
}  // namespace

