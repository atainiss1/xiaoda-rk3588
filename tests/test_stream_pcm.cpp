#include "stream_pcm.hpp"
#include <iostream>
#include <vector>

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        constexpr std::size_t sample_count = 24000;
        std::vector<unsigned char> input(sample_count * 2);
        for (std::size_t i = 0; i < sample_count; ++i)
            rkvoice::put16(input.data() + i * 2, static_cast<std::uint16_t>((i % 100) * 300 - 15000));
        // Unusual packet sizes exercise boundaries inside the 10 ms fade.
        for (const std::size_t chunk_samples : {1U, 79U, 240U, 1440U}) {
            rkvoice::StreamEnvelope envelope(24000);
            std::vector<unsigned char> output;
            for (std::size_t offset = 0; offset < input.size(); offset += chunk_samples * 2) {
                const auto count = std::min(chunk_samples * 2, input.size() - offset);
                std::vector<unsigned char> chunk(input.begin() + static_cast<std::ptrdiff_t>(offset),
                    input.begin() + static_cast<std::ptrdiff_t>(offset + count));
                auto result = envelope.push(chunk);
                output.insert(output.end(), result.begin(), result.end());
            }
            check(output.size() == input.size() - 480, "Streaming must retain only 10ms of tail");
            auto tail = envelope.finish();
            output.insert(output.end(), tail.begin(), tail.end());
            check(output.size() == input.size(), "Streaming lost or duplicated PCM");
            for (std::size_t i = 0; i < sample_count; ++i) {
                const auto ramp = std::min({std::size_t{240}, i, sample_count - 1 - i});
                const int expected = rkvoice::decode16(input.data() + i * 2) * static_cast<int>(ramp) / 240;
                check(rkvoice::decode16(output.data() + i * 2) == expected,
                      "Packet boundary changed the waveform");
            }
        }
        rkvoice::StreamEnvelope short_audio(16000);
        std::vector<unsigned char> tiny(40, 0x20);
        check(short_audio.push(tiny).empty(), "Tiny stream must hold its tail");
        auto tail = short_audio.finish();
        check(tail.size() == tiny.size() && rkvoice::decode16(tail.data()) == 0 &&
              rkvoice::decode16(tail.data() + tail.size() - 2) == 0, "Tiny stream fade failed");
        bool rejected = false;
        try { short_audio.push(tiny); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Writing after finish was accepted");
        rkvoice::StreamEnvelope invalid(16000);
        rejected = false;
        try { invalid.push({0}); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Partial PCM sample was accepted");
        std::cout << "STREAM_PCM_TEST_OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
