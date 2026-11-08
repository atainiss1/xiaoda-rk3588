#include "live_playback.hpp"
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main() {
    try {
        if (!std::getenv("RKVOICE_TEST_PLAYBACK")) { std::cerr << "Set RKVOICE_TEST_PLAYBACK=1 and a dedicated RKVOICE_SINK\n"; return 2; }
        for (int test=0;test<3;++test) {
            rkvoice::audio::turn_interrupted=false;
            const auto start=std::chrono::steady_clock::now();
            bool cancelled=false;
            auto requested=start;
            std::thread cancel;
            try {
                rkvoice::audio::LivePlayback stream(24000);
                if (test==0) { stream.write(std::vector<unsigned char>(24000,0)); stream.drain(); }
                else {
                    // Test cancellation while writing, and separately during drain.
                    if (test==2) stream.write(std::vector<unsigned char>(4800,0));
                    cancel=std::thread([&] { std::this_thread::sleep_for(5ms); requested=std::chrono::steady_clock::now(); rkvoice::audio::turn_interrupted=true; });
                    if (test==1) stream.write(std::vector<unsigned char>(24000*2*5,0));
                    stream.drain();
                }
            } catch (const std::exception&) { cancelled=rkvoice::audio::turn_interrupted; if (!cancelled) { if (cancel.joinable()) cancel.join(); throw; } }
            if (cancel.joinable()) cancel.join();
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            const auto cancelMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-requested).count();
            std::cout << "PLAYBACK_TEST case=" << test << " cancelled=" << cancelled << " elapsed_ms=" << ms << " cancel_ms=" << cancelMs << std::endl;
            if (test>0 && (!cancelled || cancelMs>800)) throw std::runtime_error("Playback cancellation exceeded bound");
        }
        rkvoice::audio::turn_interrupted=false;
        std::cout << "CANCELLABLE_PLAYBACK_TEST_OK\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
