#pragma once
#include "live_audio.hpp"
#include "speech_engine.hpp"

namespace rkvoice {
class VoiceFrontend {
public:
    enum class Mode { off, wake, listen, reply, hold };
    struct Event { enum Type { start, pcm, end } type; vad::Block samples{}; };
private:
    SpeechEngine engine_;
    LiveAudio input_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Event> events_;
    Mode mode_=Mode::off;
    unsigned epoch_=0;
    bool quit_=false;
    std::string failure_;
    std::thread thread_;
    std::atomic<bool> woke_{false};
    void run() {
        try {
            unsigned seen=~0U;
            std::deque<vad::Block> pre;
            bool speaking=false;
            int frames=0;
            auto lastWake=std::chrono::steady_clock::now()-std::chrono::seconds(10);
            while (true) {
                Mode mode; unsigned epoch;
                { std::lock_guard<std::mutex> lock(mutex_); if (quit_) break; mode=mode_; epoch=epoch_; }
                if (epoch!=seen) { engine_.reset(); pre.clear(); speaking=false; frames=0; seen=epoch; }
                vad::Block block;
                if (!input_.next(block)) continue;
                if (mode==Mode::off) continue;
                const auto result=engine_.accept(block,mode==Mode::wake || mode==Mode::reply);
                std::lock_guard<std::mutex> lock(mutex_);
                if (epoch!=epoch_ || mode!=mode_) continue;
                if (result.wake && std::chrono::steady_clock::now()-lastWake>std::chrono::seconds(2)) {
                    lastWake=std::chrono::steady_clock::now();
                    woke_=true;
                    if (mode==Mode::reply) {
                        audio::turn_interrupted=true;
                        mode_=Mode::hold; ++epoch_; // one wake event per interrupted reply
                    }
                    std::cout << "WAKE_DETECTED keyword=你好小哒 local=true during_reply="
                              << (mode==Mode::reply) << std::endl;
                    changed_.notify_all();
                }
                if (mode!=Mode::listen) continue;
                pre.push_back(block); if (pre.size()>12) pre.pop_front();
                if (!speaking && result.speech) {
                    speaking=true; frames=0;
                    events_.push_back({Event::start,{}});
                    for (const auto& b:pre) events_.push_back({Event::pcm,b});
                    std::cout << "SPEECH_START engine=silero preroll_ms=720" << std::endl;
                } else if (speaking) events_.push_back({Event::pcm,block});
                if (speaking && (!result.speech || ++frames>=334)) {
                    events_.push_back({Event::end,{}});
                    std::cout << "CAPTURE_END reason=" << (frames>=334?"max_duration":"silence")
                              << " engine=silero frames=" << frames << std::endl;
                    mode_=Mode::reply; ++epoch_; // preserve queued utterance; keep AFE fed during playback
                }
                if (events_.size()>512) throw std::runtime_error("Cloud sender fell behind capture; bounded queue exceeded");
                changed_.notify_all();
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_); failure_=e.what(); changed_.notify_all();
        }
    }
public:
    explicit VoiceFrontend(const std::string& models):engine_(models) {
        input_.active(false);
        thread_=std::thread([this]{run();});
    }
    ~VoiceFrontend() {
        input_.active(false);
        { std::lock_guard<std::mutex> lock(mutex_); quit_=true; }
        if (thread_.joinable()) thread_.join();
    }
    void mode(Mode mode) {
        // Gate software first. No stale frames from a previous session reach the queue.
        { std::lock_guard<std::mutex> lock(mutex_); mode_=Mode::off; ++epoch_; events_.clear(); }
        input_.active(mode!=Mode::off);
        { std::lock_guard<std::mutex> lock(mutex_); mode_=mode; ++epoch_; }
        changed_.notify_all();
    }
    bool takeWake() { return woke_.exchange(false); }
    bool next(Event& event) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock,std::chrono::milliseconds(100),[&]{return !events_.empty()||!failure_.empty()||audio::cancelled();});
        if (!failure_.empty()) throw std::runtime_error(failure_);
        if (audio::cancelled() || events_.empty()) return false;
        event=events_.front(); events_.pop_front(); return true;
    }
    void check() { std::lock_guard<std::mutex> lock(mutex_); if (!failure_.empty()) throw std::runtime_error(failure_); }
};
}
