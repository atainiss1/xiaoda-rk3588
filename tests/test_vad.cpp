#include "vad.hpp"
#include "audio.hpp"
#include <iostream>
#include <vector>

using namespace rkvoice::vad;
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void begin(Endpointer& endpoint) {
    check(endpoint.push(true) == Event::waiting, "One frame must not start speech");
    check(endpoint.push(true) == Event::waiting, "Two frames must not start speech");
    check(endpoint.push(true) == Event::waiting, "Three frames must not start speech");
    check(endpoint.push(true) == Event::waiting, "Four frames must not start speech");
    check(endpoint.push(true) == Event::started, "Sustained speech must start");
}
int main(int argc, char** argv) {
    try {
        Detector detector;
        Block zero{};
        for (int i = 0; i < 100; ++i) check(!detector.speech(zero), "Zero PCM detected as speech");
        bool rejected = false;
        try { Detector invalid(4); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "Invalid VAD mode accepted");

        Endpointer empty(30, 1);
        for (int i = 0; i < 16; ++i)
            check(empty.push(false) == Event::waiting, "No-speech timeout fired early");
        check(empty.push(false) == Event::no_speech, "Silence must time out without submission");

        Endpointer noise(30);
        for (int i = 0; i < 50; ++i)
            check(noise.push(i % 10 == 0) == Event::waiting, "Isolated bursts must not start");
        Endpointer brief_noise(30);
        for (int i = 0; i < 50; ++i)
            check(brief_noise.push(i % 12 < 4) == Event::waiting,
                  "A 240 ms disturbance must not open a turn");

        Endpointer pauses(30);
        begin(pauses);
        for (int i = 0; i < 19; ++i)
            check(pauses.push(false) == Event::speaking, "Short pause truncated speech");
        check(pauses.push(true) == Event::speaking, "Speech must reset trailing silence");
        for (int i = 0; i < 19; ++i)
            check(pauses.push(false) == Event::speaking, "Silence counter did not reset");
        check(pauses.push(false) == Event::silence, "Trailing silence did not finish turn");

        Endpointer capped(1);
        begin(capped);
        for (int i = 0; i < 15; ++i)
            check(capped.push(true) == Event::speaking, "Maximum duration fired early");
        check(capped.push(true) == Event::limit, "Continuous speech must be bounded");

        PreRoll ring;
        for (int i = 0; i < 15; ++i) { Block block{}; block[0] = static_cast<std::int16_t>(i); ring.push(block); }
        check(ring.size() == 10, "Pre-roll must remain bounded");
        std::vector<int> sent;
        ring.flush([&](const Block& block) { sent.push_back(block[0]); });
        check(sent.size() == 10 && sent.front() == 5 && sent.back() == 14,
              "Pre-roll lost or reordered onset audio");
        ring.flush([&](const Block& block) { sent.push_back(block[0]); });
        check(sent.size() == 10, "Pre-roll audio was sent twice");

        NoiseGuard guard({100, 102, 98, 101, 99, 4000, 100});
        Endpointer guarded(30);
        Block low_noise{};
        low_noise.fill(105);
        for (int i = 0; i < 30; ++i)
            check(guarded.push(guard.speech(true, low_noise, false)) == Event::waiting,
                  "WebRTC false positives on low noise must not start speech");
        low_noise.fill(450);
        for (int i = 0; i < 30; ++i)
            check(guarded.push(guard.speech(true, low_noise, false)) == Event::waiting,
                  "Measured ambient-sized bursts must not start even if WebRTC accepts them");
        Block loud{};
        loud.fill(2000);
        check(!guard.speech(false, loud, false), "Energy alone must not bypass WebRTC");
        check(guard.speech(true, loud, false), "Speech above noise floor was rejected");
        Block quiet_ending{};
        quiet_ending.fill(250);
        check(!guard.speech(true, quiet_ending, false) && guard.speech(true, quiet_ending, true),
              "Onset/continuation hysteresis failed");

        // Real speech from the pinned upstream fixture, surrounded by silence:
        // exercises resampling/classification + endpoint timing together.
        check(argc == 2, "Expected upstream speech fixture path");
        auto wave = rkvoice::audio::load_wave(argv[1]);
        check(wave.sample_rate == 16000 && wave.channels == 1, "Unexpected fixture format");
        Detector real_detector;
        NoiseGuard real_guard({});
        Endpointer real_endpoint(30);
        for (int i = 0; i < 50; ++i)
            check(real_endpoint.push(real_detector.speech(zero)) == Event::waiting, "Leading silence triggered speech");
        int starts = 0;
        int active = 0;
        for (std::size_t offset = 0; offset + 1920 <= wave.pcm.size(); offset += 1920) {
            Block block{};
            for (std::size_t i = 0; i < block.size(); ++i)
                block[i] = static_cast<std::int16_t>(rkvoice::decode16(wave.pcm.data() + offset + i * 2));
            const bool speech = real_guard.speech(real_detector.speech(block), block, starts != 0);
            active += speech ? 1 : 0;
            const auto event = real_endpoint.push(speech);
            if (event == Event::started) ++starts;
            check(event == Event::waiting || event == Event::started || event == Event::speaking,
                  "Speech fixture was prematurely truncated");
        }
        check(starts == 1 && active > 60, "Actual speech was not detected");
        bool ended = false;
        for (int i = 0; i < 40; ++i) {
            if (real_endpoint.push(real_detector.speech(zero)) == Event::silence) { ended = true; break; }
        }
        check(ended, "Speech plus trailing silence did not finish automatically");
        std::cout << "VAD_ENDPOINT_TEST_OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
