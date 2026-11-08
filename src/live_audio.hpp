#pragma once
#include "audio.hpp"
#include "vad.hpp"
#include <pulse/pulseaudio.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace rkvoice {
// One persistent Pulse stream. No process-global alarm is used by its callbacks.
// The server thread never waits on a network operation or the speech model.
class LiveAudio {
    pa_threaded_mainloop* loop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<vad::Block> blocks_;
    vad::Block partial_{};
    std::size_t used_ = 0;
    bool active_ = false;
    std::string failure_;
    unsigned long dropped_ = 0;
    static void read_cb(pa_stream* stream, std::size_t, void* self) {
        static_cast<LiveAudio*>(self)->read(stream);
    }
    static void state_cb(pa_stream* stream, void* self) {
        if (pa_stream_get_state(stream)==PA_STREAM_FAILED || pa_stream_get_state(stream)==PA_STREAM_TERMINATED) {
            auto* that=static_cast<LiveAudio*>(self);
            std::lock_guard<std::mutex> lock(that->mutex_);
            that->failure_="Pulse capture disconnected"; that->ready_.notify_all();
        }
    }
    void read(pa_stream* stream) {
        const void* data = nullptr;
        std::size_t bytes = 0;
        if (pa_stream_peek(stream, &data, &bytes) < 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_ = "Pulse capture peek failed"; ready_.notify_all(); return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_) {
                const auto* p = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i + 1 < bytes; i += 2) {
                    // Input is the mono left-channel AFE output, not a stereo average.
                    std::uint64_t clipped = 0;
                    partial_[used_++] = static_cast<std::int16_t>(amplify(p ? decode16(p+i) : 0, 4, clipped));
                    if (used_ == partial_.size()) {
                        if (blocks_.size() == 32) { blocks_.pop_front(); ++dropped_; }
                        blocks_.push_back(partial_); used_ = 0;
                    }
                }
            }
        }
        if (bytes) pa_stream_drop(stream);
        ready_.notify_all();
    }
    template<class Test> void wait(Test test) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!test()) {
            if (audio::stopped || std::chrono::steady_clock::now() > end)
                throw std::runtime_error("Pulse stream readiness timed out");
            pa_threaded_mainloop_unlock(loop_);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            pa_threaded_mainloop_lock(loop_);
        }
    }
    void release() {
        if (loop_) pa_threaded_mainloop_stop(loop_);
        if (stream_) { pa_stream_disconnect(stream_); pa_stream_unref(stream_); }
        if (context_) { pa_context_disconnect(context_); pa_context_unref(context_); }
        if (loop_) pa_threaded_mainloop_free(loop_);
        stream_=nullptr; context_=nullptr; loop_=nullptr;
    }
public:
    LiveAudio() {
        loop_ = pa_threaded_mainloop_new();
        if (!loop_) throw std::runtime_error("Pulse mainloop allocation failed");
        context_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "rkvoice-persistent-capture");
        if (!context_ || pa_threaded_mainloop_start(loop_) < 0) { release(); throw std::runtime_error("Pulse mainloop failed"); }
        pa_threaded_mainloop_lock(loop_);
        try {
            if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
                throw std::runtime_error("Pulse context connect failed");
            wait([&] {
                auto s=pa_context_get_state(context_);
                if (!PA_CONTEXT_IS_GOOD(s)) throw std::runtime_error("Pulse context unavailable");
                return s==PA_CONTEXT_READY;
            });
            pa_sample_spec spec{PA_SAMPLE_S16LE,16000,1};
            pa_channel_map map; pa_channel_map_init_mono(&map);
            stream_=pa_stream_new(context_,"persistent microphone",&spec,&map);
            if (!stream_) throw std::runtime_error("Pulse capture allocation failed");
            pa_stream_set_read_callback(stream_,read_cb,this);
            pa_stream_set_state_callback(stream_,state_cb,this);
            pa_buffer_attr attr{static_cast<uint32_t>(-1),static_cast<uint32_t>(-1),static_cast<uint32_t>(-1),static_cast<uint32_t>(-1),640};
            if (pa_stream_connect_record(stream_,audio::device(true),&attr,
                    static_cast<pa_stream_flags_t>(PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY))<0)
                throw std::runtime_error("Pulse capture connect failed");
            wait([&] {
                auto s=pa_stream_get_state(stream_);
                if (!PA_STREAM_IS_GOOD(s)) throw std::runtime_error("Pulse capture unavailable");
                return s==PA_STREAM_READY;
            });
            pa_threaded_mainloop_unlock(loop_);
        } catch (...) { pa_threaded_mainloop_unlock(loop_); release(); throw; }
    }
    ~LiveAudio() { release(); }
    LiveAudio(const LiveAudio&)=delete;
    LiveAudio& operator=(const LiveAudio&)=delete;
    void active(bool enabled) {
        pa_threaded_mainloop_lock(loop_);
        { std::lock_guard<std::mutex> lock(mutex_); active_=false; blocks_.clear(); used_=0; }
        if (std::string(audio::device(true))=="rkvoice_clean") {
            auto* suspension=pa_context_suspend_source_by_name(context_,"rkvoice_clean",enabled?0:1,nullptr,nullptr);
            if (suspension) pa_operation_unref(suspension);
        }
        auto* flush=pa_stream_flush(stream_,nullptr,nullptr);
        if (flush) pa_operation_unref(flush);
        auto* cork=pa_stream_cork(stream_,enabled?0:1,nullptr,nullptr);
        if (cork) pa_operation_unref(cork);
        { std::lock_guard<std::mutex> lock(mutex_); active_=enabled; }
        pa_threaded_mainloop_unlock(loop_);
        ready_.notify_all();
    }
    bool next(vad::Block& block) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait_for(lock,std::chrono::milliseconds(100),[&]{return !blocks_.empty()||!failure_.empty();});
        if (!failure_.empty()) throw std::runtime_error(failure_);
        if (dropped_) throw std::runtime_error("Speech processing fell behind capture; audio was not submitted");
        if (blocks_.empty()) return false;
        block=blocks_.front(); blocks_.pop_front(); return true;
    }
};
}
