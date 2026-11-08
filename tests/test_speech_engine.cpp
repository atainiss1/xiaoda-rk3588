#include "audio.hpp"
#include "speech_engine.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if (argc<2) return 2;
    rkvoice::SpeechEngine engine(argv[1]);
    rkvoice::vad::Block zero{};
    for (int i=0;i<1000;++i) {
        const auto r=engine.accept(zero,true);
        if (r.speech || r.wake) throw std::runtime_error("Silence triggered speech or keyword");
    }
    engine.reset();
    std::cout << "SPEECH_ENGINE_SILENCE_OK seconds=60\n";
    if (argc>=3) {
        const int gain=argc>=4?std::stoi(argv[3]):1;
        if(gain<1 || gain>8) throw std::runtime_error("Test gain must be 1..8");
        const auto wav=rkvoice::audio::load_wave(argv[2]);
        if (wav.sample_rate!=16000) throw std::runtime_error("Test WAV must be 16 kHz");
        int speech=0,wake=0;
        for (std::size_t offset=0; offset<wav.pcm.size(); offset+=960*2*wav.channels) {
            rkvoice::vad::Block b{};
            for (std::size_t i=0;i<b.size();++i) {
                const auto at=offset+i*2*wav.channels;
                if (at+1<wav.pcm.size()) {
                    std::uint64_t clipped=0;
                    b[i]=static_cast<int16_t>(rkvoice::amplify(rkvoice::decode16(wav.pcm.data()+at),gain,clipped));
                }
            }
            const auto r=engine.accept(b,true); speech+=r.speech?1:0; wake+=r.wake?1:0;
            if(r.wake) std::cout << "FILE_WAKE seconds=" << double(offset)/(32000*wav.channels) << '\n';
        }
        for(int i=0;i<20;++i) { const auto r=engine.accept(zero,true); speech+=r.speech?1:0; wake+=r.wake?1:0; }
        std::cout << "SPEECH_FILE_RESULT speech_blocks=" << speech << " wake_count=" << wake << '\n';
    }
}
