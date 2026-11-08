#pragma once
#include <fvad.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <cmath>
#include <vector>

namespace rkvoice::vad {
// Transport uses 60 ms Opus frames. WebRTC only accepts 10/20/30 ms,
// so classify both 30 ms halves without dropping samples.
constexpr int block_ms = 60;
using Block = std::array<std::int16_t, 960>;
inline double rms(const Block& pcm) {
    std::int64_t square_sum = 0;
    for (const auto sample : pcm) square_sum += static_cast<std::int64_t>(sample) * sample;
    return std::sqrt(static_cast<double>(square_sum) / static_cast<double>(pcm.size()));
}

// WebRTC can label very quiet board noise as speech. Require an onset to rise
// above the measured noise floor, then use a lower floor to retain quiet endings.
// This is an additional guard, not a replacement for WebRTC classification.
class NoiseGuard {
    double noise_ = 0;
public:
    explicit NoiseGuard(std::vector<double> calibration) {
        if (!calibration.empty()) {
            std::sort(calibration.begin(), calibration.end());
            noise_ = calibration[calibration.size() / 2];
        }
    }
    double noise() const { return noise_; }
    // Calibrated for this board's 24 dB + x4 input: observed ambient peaks
    // exceeded 400 RMS, while both normal spoken test phrases clear 800.
    double onset() const { return std::max(800.0, noise_ * 6.0); }
    double continuation() const { return std::max(120.0, noise_ * 2.0); }
    bool speech(bool classified, const Block& pcm, bool active) const {
        return classified && rms(pcm) >= (active ? continuation() : onset());
    }
};

class Detector {
    std::unique_ptr<Fvad, decltype(&fvad_free)> detector_{fvad_new(), fvad_free};
public:
    explicit Detector(int mode = 2) {
        if (!detector_ || fvad_set_sample_rate(detector_.get(), 16000) != 0 ||
            fvad_set_mode(detector_.get(), mode) != 0)
            throw std::runtime_error("WebRTC VAD initialization failed");
    }
    bool speech(const Block& pcm) {
        const int first = fvad_process(detector_.get(), pcm.data(), 480);
        const int second = fvad_process(detector_.get(), pcm.data() + 480, 480);
        if (first < 0 || second < 0) throw std::runtime_error("Invalid WebRTC VAD frame");
        return first != 0 || second != 0;
    }
};

enum class Event { waiting, started, speaking, silence, limit, no_speech };

// Endpointer policy is separate from the classifier, so timing can be tested
// deterministically, including short pauses and false starts.
class Endpointer {
    // Require 300 ms of accepted speech within 480 ms. A short disturbance
    // followed by WebRTC's internal hangover must not open a cloud turn.
    std::array<bool, 8> recent_{};
    std::size_t cursor_ = 0;
    int waiting_blocks_ = 0, speech_blocks_ = 0, silent_blocks_ = 0;
    int maximum_blocks_, wait_blocks_;
    bool started_ = false, finished_ = false;
public:
    explicit Endpointer(int maximum_seconds, int wait_seconds = 60)
        : maximum_blocks_((maximum_seconds * 1000 + block_ms - 1) / block_ms),
          wait_blocks_((wait_seconds * 1000 + block_ms - 1) / block_ms) {
        if (maximum_seconds < 1 || maximum_seconds > 60 || wait_seconds < 1 || wait_seconds > 60)
            throw std::runtime_error("VAD duration must be 1..60 seconds");
    }
    Event push(bool speech) {
        if (finished_) throw std::logic_error("VAD turn already finished");
        if (!started_) {
            recent_[cursor_] = speech;
            cursor_ = (cursor_ + 1) % recent_.size();
            ++waiting_blocks_;
            if (std::count(recent_.begin(), recent_.end(), true) >= 5) {
                started_ = true;
                speech_blocks_ = 1;
                return Event::started;
            }
            if (waiting_blocks_ >= wait_blocks_) {
                finished_ = true;
                return Event::no_speech;
            }
            return Event::waiting;
        }
        ++speech_blocks_;
        silent_blocks_ = speech ? 0 : silent_blocks_ + 1;
        if (silent_blocks_ >= 20) {
            finished_ = true;
            return Event::silence;
        }
        if (speech_blocks_ >= maximum_blocks_) {
            finished_ = true;
            return Event::limit;
        }
        return Event::speaking;
    }
};

// Includes the current frame. Flush once at onset, then send new frames directly.
class PreRoll {
    std::deque<Block> blocks_;
public:
    void push(const Block& block) {
        if (blocks_.size() == 10) blocks_.pop_front();
        blocks_.push_back(block);
    }
    template<class Send> void flush(Send&& send) {
        for (const auto& block : blocks_) send(block);
        blocks_.clear();
    }
    std::size_t size() const { return blocks_.size(); }
};
} // namespace rkvoice::vad
