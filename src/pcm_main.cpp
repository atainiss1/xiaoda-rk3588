#include "pcm.hpp"

namespace {
using namespace rkvoice;
void convert(const char* input, const char* output, int gain, int skip_ms) {
    std::ifstream in(input, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open input");
    const auto length = in.tellg();
    if (length < 0) throw std::runtime_error("cannot determine input size");
    const auto bytes = static_cast<std::uint64_t>(length);
    if (bytes % 4 != 0) throw std::runtime_error("input is not complete stereo s16le frames");
    const auto skip = static_cast<std::uint64_t>(skip_ms) * rate / 1000;
    if (bytes / 4 <= skip) throw std::runtime_error("no audio remains after skip");
    const auto frames = bytes / 4 - skip;
    if (frames > (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2)
        throw std::runtime_error("input too large for standard WAV");
    in.seekg(static_cast<std::streamoff>(skip * 4));
    if (!in) throw std::runtime_error("input seek failed");
    OutputFile out(output);
    const auto header = wav_header(static_cast<std::uint32_t>(frames * 2));
    out.write(header.data(), header.size());
    std::array<unsigned char, 4096> stereo{};
    std::array<unsigned char, 2048> mono{};
    std::uint64_t remaining = frames;
    std::uint64_t clipped = 0;
    double sum_squares = 0;
    int input_peak = 0;
    int output_peak = 0;
    while (remaining > 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, stereo.size() / 4));
        in.read(reinterpret_cast<char*>(stereo.data()), static_cast<std::streamsize>(count * 4));
        if (in.gcount() != static_cast<std::streamsize>(count * 4))
            throw std::runtime_error("input shortened or read failed; output is incomplete");
        for (std::size_t i = 0; i < count; ++i) {
            const int left = decode16(stereo.data() + i * 4);
            const int value = amplify(left, gain, clipped);
            sum_squares += static_cast<double>(left) * left;
            input_peak = std::max(input_peak, std::abs(left));
            output_peak = std::max(output_peak, std::abs(value));
            put16(mono.data() + i * 2, static_cast<std::uint16_t>(value));
        }
        out.write(mono.data(), count * 2);
        remaining -= count;
    }
    out.finish();
    std::cout << "rate=" << rate << " channels=1 frames=" << frames
              << " seconds=" << static_cast<double>(frames) / rate
              << " gain=" << gain << " skip_ms=" << skip_ms
              << " input_rms=" << std::sqrt(sum_squares / static_cast<double>(frames))
              << " input_peak=" << input_peak << " output_peak=" << output_peak
              << " clipped_samples=" << clipped << '\n';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            self_test();
            return 0;
        }
        if (argc < 3 || argc > 5) {
            std::cerr << "Usage: rkvoice_pcm INPUT.raw OUTPUT.wav [gain=4] [skip_ms=500]\n"
                      << "Input: 16000 Hz stereo interleaved s16le; use left channel only.\n"
                      << "Or: rkvoice_pcm --self-test\n";
            return 2;
        }
        convert(argv[1], argv[2], argc >= 4 ? number(argv[3], 1, 16) : 4,
                argc >= 5 ? number(argv[4], 0, 60000) : 500);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rkvoice_pcm: " << error.what() << '\n';
        return 1;
    }
}
