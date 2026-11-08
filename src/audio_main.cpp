#include "audio.hpp"
using namespace rkvoice;
using namespace rkvoice::audio;

int main(int argc, char** argv) {
    std::signal(SIGINT, on_stop);
    std::signal(SIGTERM, on_stop);
    std::signal(SIGALRM, on_timeout);
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (argc == 3 && command == "inspect") inspect(load_wave(argv[2]));
        else if (argc == 3 && command == "play") play(argv[2]);
        else if (argc == 2 && command == "tone") tone();
        else if ((argc == 4 || argc == 5) && (command == "record" || command == "roundtrip")) {
            const int seconds = number(argv[3], 1, 120);
            const int gain = argc == 5 ? number(argv[4], 1, 16) : 4;
            if (!stopped) record(argv[2], seconds, gain);
            if (command == "roundtrip" && !stopped) play(argv[2]);
        } else {
            std::cerr << "Usage: rkvoice_audio record|roundtrip OUTPUT.wav SECONDS [GAIN=4]\n"
                      << "       rkvoice_audio play|inspect INPUT.wav\n"
                      << "       rkvoice_audio tone\n"
                      << "Optional device overrides: RKVOICE_SOURCE, RKVOICE_SINK.\n";
            return 2;
        }
        return stopped ? 130 : 0;
    } catch (const std::exception& error) {
        std::cerr << "rkvoice_audio: " << error.what() << '\n';
        return 1;
    }
}
