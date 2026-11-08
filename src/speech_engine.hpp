#pragma once
#include "vad.hpp"
#include "wake_match.hpp"
#include <sherpa-c-api.h>
#include <string>

namespace rkvoice {
class SpeechEngine {
    const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;
    const SherpaOnnxKeywordSpotter* kws_ = nullptr;
    const SherpaOnnxOnlineStream* stream_ = nullptr;
public:
    explicit SpeechEngine(const std::string& directory) {
        const auto vadPath=directory+"/silero_vad.onnx";
        SherpaOnnxVadModelConfig v{};
        v.silero_vad.model=vadPath.c_str();
        v.silero_vad.threshold=0.5f;
        v.silero_vad.min_silence_duration=0.9f;
        v.silero_vad.min_speech_duration=0.2f;
        v.silero_vad.window_size=512;
        v.silero_vad.max_speech_duration=20;
        v.sample_rate=16000; v.num_threads=1; v.provider="cpu";
        vad_=SherpaOnnxCreateVoiceActivityDetector(&v,30);
        if (!vad_) throw std::runtime_error("Silero VAD model initialization failed");
        const auto base=directory+"/kws/";
        const auto encoder=base+"encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx";
        const auto decoder=base+"decoder-epoch-12-avg-2-chunk-16-left-64.onnx";
        const auto joiner=base+"joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx";
        const auto tokens=base+"tokens.txt";
        const auto keywords=directory+"/keywords.txt";
        SherpaOnnxKeywordSpotterConfig k{};
        k.feat_config.sample_rate=16000; k.feat_config.feature_dim=80;
        k.model_config.transducer.encoder=encoder.c_str();
        k.model_config.transducer.decoder=decoder.c_str();
        k.model_config.transducer.joiner=joiner.c_str();
        k.model_config.tokens=tokens.c_str(); k.model_config.num_threads=1; k.model_config.provider="cpu";
        k.max_active_paths=4; k.num_trailing_blanks=1;
        k.keywords_score=1.5f; k.keywords_threshold=0.35f; k.keywords_file=keywords.c_str();
        kws_=SherpaOnnxCreateKeywordSpotter(&k);
        if (kws_) stream_=SherpaOnnxCreateKeywordStream(kws_);
        if (!kws_ || !stream_) { clear(); throw std::runtime_error("Wake keyword model initialization failed"); }
    }
    ~SpeechEngine() { clear(); }
    void clear() {
        if (stream_) SherpaOnnxDestroyOnlineStream(stream_);
        if (kws_) SherpaOnnxDestroyKeywordSpotter(kws_);
        if (vad_) SherpaOnnxDestroyVoiceActivityDetector(vad_);
        stream_=nullptr; kws_=nullptr; vad_=nullptr;
    }
    void reset() {
        SherpaOnnxVoiceActivityDetectorReset(vad_);
        SherpaOnnxResetKeywordStream(kws_,stream_);
    }
    struct Result { bool speech; bool wake; };
    Result accept(const vad::Block& block, bool keywords) {
        std::array<float,960> samples{};
        for (std::size_t i=0;i<samples.size();++i) samples[i]=block[i]/32768.0f;
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_,samples.data(),960);
        const bool speech=SherpaOnnxVoiceActivityDetectorDetected(vad_)!=0;
        while (!SherpaOnnxVoiceActivityDetectorEmpty(vad_)) SherpaOnnxVoiceActivityDetectorPop(vad_);
        bool wake=false;
        if (keywords) {
            SherpaOnnxOnlineStreamAcceptWaveform(stream_,16000,samples.data(),960);
            while (SherpaOnnxIsKeywordStreamReady(kws_,stream_)) {
                SherpaOnnxDecodeKeywordStream(kws_,stream_);
                const auto* r=SherpaOnnxGetKeywordResult(kws_,stream_);
                const bool candidate=r && r->keyword && *r->keyword;
                if (candidate) {
                    wake=compact_wake_phrase(r->timestamps,static_cast<std::size_t>(r->count));
                    if (std::getenv("RKVOICE_KWS_TRACE")) std::cout << "KWS_CANDIDATE accepted=" << wake << ' ' << r->json << std::endl;
                }
                if (r) SherpaOnnxDestroyKeywordResult(r);
                if (candidate) { SherpaOnnxResetKeywordStream(kws_,stream_); break; }
            }
        }
        return {speech,wake};
    }
};
}
