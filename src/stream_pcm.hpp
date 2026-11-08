#pragma once
#include "pcm.hpp"
#include <vector>

namespace rkvoice {
// Mono PCM16 envelope with only a 10 ms lookahead. Packet boundaries never
// restart the fade, and the held tail can be faded once TTS completion arrives.
class StreamEnvelope {
    std::size_t fade_, emitted_ = 0;
    std::vector<unsigned char> tail_;
    bool finished_ = false;
    std::vector<unsigned char> emit(std::size_t samples, bool ending) {
        std::vector<unsigned char> result(samples * 2);
        for (std::size_t i = 0; i < samples; ++i) {
            auto ramp = std::min(fade_, emitted_ + i);
            if (ending) ramp = std::min(ramp, samples - 1 - i);
            const auto value = static_cast<int>(
                static_cast<long long>(decode16(tail_.data() + i * 2)) *
                static_cast<long long>(ramp) / static_cast<long long>(fade_));
            put16(result.data() + i * 2, static_cast<std::uint16_t>(value));
        }
        emitted_ += samples;
        tail_.erase(tail_.begin(), tail_.begin() + static_cast<std::ptrdiff_t>(samples * 2));
        return result;
    }
public:
    explicit StreamEnvelope(int rate) : fade_(static_cast<std::size_t>(rate / 100)) {
        if (rate < 8000 || rate > 48000) throw std::runtime_error("Unsupported streaming sample rate");
    }
    std::vector<unsigned char> push(const std::vector<unsigned char>& pcm) {
        if (finished_ || pcm.size() % 2 != 0) throw std::runtime_error("Invalid streaming PCM");
        tail_.insert(tail_.end(), pcm.begin(), pcm.end());
        const auto available = tail_.size() / 2;
        return emit(available > fade_ ? available - fade_ : 0, false);
    }
    std::vector<unsigned char> finish() {
        if (finished_) throw std::runtime_error("PCM stream already finished");
        finished_ = true;
        return emit(tail_.size() / 2, true);
    }
};
} // namespace rkvoice
