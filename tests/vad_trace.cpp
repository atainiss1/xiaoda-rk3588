// Local-only diagnostic: no cloud libraries or device identity.
#include "audio.hpp"
#include "vad.hpp"
#include <sstream>

using namespace rkvoice;
int main(int argc, char** argv) {
    std::signal(SIGINT, audio::on_stop);
    std::signal(SIGTERM, audio::on_stop);
    std::signal(SIGALRM, audio::on_timeout);
    try {
        if (argc != 3) return 2;
        const int blocks = number(argv[2], 2, 300) * 1000 / vad::block_ms;
        const std::string prefix = argv[1];
        OutputFile stereo_file((prefix + ".stereo.wav").c_str());
        OutputFile mono_file((prefix + ".mono.wav").c_str());
        OutputFile trace_file((prefix + ".csv").c_str());
        std::vector<unsigned char> stereo_pcm, mono_pcm;
        stereo_pcm.reserve(static_cast<std::size_t>(blocks) * 3840);
        mono_pcm.reserve(static_cast<std::size_t>(blocks) * 1920);
        std::ostringstream trace;
        trace << "frame,time_ms,rms,left_rms,right_rms,raw,accepted,active_before,event,clipped\n";
        auto stream = audio::connect(true, 16000, 2);
        vad::Detector detector;
        vad::Endpointer endpoint(20);
        std::vector<double> calibration;
        std::unique_ptr<vad::NoiseGuard> guard;
        bool active = false;
        int starts = 0;
        std::uint64_t clipped = 0;
        for (int index = 0; index < blocks && !audio::stopped; ++index) {
            std::array<unsigned char, 3840> stereo{};
            std::array<unsigned char, 1920> encoded{};
            vad::Block mono{}, left{}, right{};
            audio::read_audio(stream.get(), stereo.data(), stereo.size());
            const auto previous_clipped = clipped;
            for (std::size_t i = 0; i < mono.size(); ++i) {
                left[i] = static_cast<std::int16_t>(decode16(stereo.data() + i * 4));
                right[i] = static_cast<std::int16_t>(decode16(stereo.data() + i * 4 + 2));
                mono[i] = static_cast<std::int16_t>(amplify(left[i], 4, clipped));
                put16(encoded.data() + i * 2, static_cast<std::uint16_t>(mono[i]));
            }
            stereo_pcm.insert(stereo_pcm.end(), stereo.begin(), stereo.end());
            mono_pcm.insert(mono_pcm.end(), encoded.begin(), encoded.end());
            const bool raw = detector.speech(mono);
            bool accepted = false;
            const bool before = active;
            int event_code = -1;
            if (index < 20) {
                if (index >= 5) calibration.push_back(vad::rms(mono));
                if (index == 19) {
                    guard = std::make_unique<vad::NoiseGuard>(calibration);
                    std::cout << "LOCAL_TRACE_READY noise=" << guard->noise()
                              << " onset=" << guard->onset() << " no_cloud=true" << std::endl;
                }
            } else {
                accepted = guard->speech(raw, mono, active);
                const auto event = endpoint.push(accepted);
                event_code = static_cast<int>(event);
                if (event == vad::Event::started) {
                    active = true;
                    ++starts;
                    std::cout << "LOCAL_START time_ms=" << index * 60
                              << " rms=" << vad::rms(mono) << std::endl;
                }
                if (event == vad::Event::silence || event == vad::Event::limit ||
                    event == vad::Event::no_speech) {
                    std::cout << "LOCAL_END time_ms=" << index * 60
                              << " event=" << event_code << std::endl;
                    active = false;
                    endpoint = vad::Endpointer(20);
                }
            }
            trace << index << ',' << index * 60 << ',' << vad::rms(mono) << ','
                  << vad::rms(left) << ',' << vad::rms(right) << ',' << raw << ','
                  << accepted << ',' << before << ',' << event_code << ','
                  << clipped - previous_clipped << '\n';
        }
        stream.reset();
        auto stereo_header = wav_header(static_cast<std::uint32_t>(stereo_pcm.size()));
        put16(stereo_header.data() + 22, 2);
        put32(stereo_header.data() + 28, 64000);
        put16(stereo_header.data() + 32, 4);
        stereo_file.write(stereo_header.data(), stereo_header.size());
        stereo_file.write(stereo_pcm.data(), stereo_pcm.size());
        stereo_file.finish();
        const auto mono_header = wav_header(static_cast<std::uint32_t>(mono_pcm.size()));
        mono_file.write(mono_header.data(), mono_header.size());
        mono_file.write(mono_pcm.data(), mono_pcm.size());
        mono_file.finish();
        const auto data = trace.str();
        trace_file.write(reinterpret_cast<const unsigned char*>(data.data()), data.size());
        trace_file.finish();
        std::cout << "LOCAL_TRACE_DONE starts=" << starts << " clipped=" << clipped
                  << " seconds=" << mono_pcm.size() / 32000.0 << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
