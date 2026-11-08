#pragma once
#include "audio.hpp"
#include <pulse/pulseaudio.h>
#include <thread>

namespace rkvoice::audio {
// Playback waits are cancellable, including drain. No process-wide alarm is used.
class LivePlayback {
    pa_threaded_mainloop* loop_=nullptr;
    pa_context* context_=nullptr;
    pa_stream* stream_=nullptr;
    bool running_=false, finished_=false;
    struct Lock {
        pa_threaded_mainloop* loop;
        explicit Lock(pa_threaded_mainloop* p):loop(p) { pa_threaded_mainloop_lock(loop); }
        ~Lock() { pa_threaded_mainloop_unlock(loop); }
    };
    void pause() {
        pa_threaded_mainloop_unlock(loop_);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pa_threaded_mainloop_lock(loop_);
    }
    template<class Test> void wait(Test test, int milliseconds, const char* error) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        while (!test()) {
            if (cancelled()) throw std::runtime_error("Playback cancelled");
            if (std::chrono::steady_clock::now()>=end) throw std::runtime_error(error);
            pause();
        }
        if (cancelled()) throw std::runtime_error("Playback cancelled");
    }
    void healthy() {
        if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(context_)) ||
            !PA_STREAM_IS_GOOD(pa_stream_get_state(stream_)))
            throw std::runtime_error("Playback device disconnected");
    }
    void release() noexcept {
        if (running_) pa_threaded_mainloop_stop(loop_);
        if (stream_) { pa_stream_disconnect(stream_); pa_stream_unref(stream_); }
        if (context_) { pa_context_disconnect(context_); pa_context_unref(context_); }
        if (loop_) pa_threaded_mainloop_free(loop_);
        loop_=nullptr; context_=nullptr; stream_=nullptr; running_=false;
    }
public:
    explicit LivePlayback(std::uint32_t rate) {
        loop_=pa_threaded_mainloop_new();
        if (!loop_) throw std::runtime_error("Playback mainloop allocation failed");
        try {
            context_=pa_context_new(pa_threaded_mainloop_get_api(loop_),"rkvoice-cancellable-playback");
            if (!context_ || pa_threaded_mainloop_start(loop_)<0)
                throw std::runtime_error("Playback mainloop start failed");
            running_=true;
            Lock lock(loop_);
            if (pa_context_connect(context_,nullptr,PA_CONTEXT_NOFLAGS,nullptr)<0)
                throw std::runtime_error("Playback context connection failed");
            wait([&] {
                const auto s=pa_context_get_state(context_);
                if (!PA_CONTEXT_IS_GOOD(s)) throw std::runtime_error("Playback server unavailable");
                return s==PA_CONTEXT_READY;
            },3000,"Playback context timed out");
            pa_sample_spec spec{PA_SAMPLE_S16LE,rate,1};
            pa_channel_map map; pa_channel_map_init_mono(&map);
            stream_=pa_stream_new(context_,"interruptible speaker",&spec,&map);
            if (!stream_) throw std::runtime_error("Playback stream allocation failed");
            pa_buffer_attr attr{rate*2,rate*2*120/1000,static_cast<uint32_t>(-1),rate*2/100,static_cast<uint32_t>(-1)};
            if (pa_stream_connect_playback(stream_,device(false),&attr,PA_STREAM_ADJUST_LATENCY,nullptr,nullptr)<0)
                throw std::runtime_error("Playback stream connect failed");
            wait([&] { healthy(); return pa_stream_get_state(stream_)==PA_STREAM_READY; },3000,"Playback stream timed out");
        } catch (...) { release(); throw; }
    }
    ~LivePlayback() { if (!finished_) cancel(); release(); }
    LivePlayback(const LivePlayback&)=delete;
    LivePlayback& operator=(const LivePlayback&)=delete;
    void write(const std::vector<unsigned char>& bytes) {
        Lock lock(loop_);
        std::size_t offset=0;
        while (offset<bytes.size()) {
            std::size_t available=0;
            wait([&] {
                healthy(); available=pa_stream_writable_size(stream_);
                if (available==static_cast<std::size_t>(-1)) throw std::runtime_error("Playback buffer unavailable");
                return available>=2;
            },3000,"Playback device stalled");
            const auto count=std::min(available&~std::size_t{1},bytes.size()-offset);
            if (pa_stream_write(stream_,bytes.data()+offset,count,nullptr,0,PA_SEEK_RELATIVE)<0)
                throw std::runtime_error("Playback write failed");
            offset+=count;
        }
    }
    void drain() {
        Lock lock(loop_);
        int result=0;
        auto* op=pa_stream_drain(stream_,[](pa_stream*,int success,void* value) {
            *static_cast<int*>(value)=success?1:-1;
        },&result);
        if (!op) throw std::runtime_error("Playback drain unavailable");
        try {
            wait([&] { healthy(); return pa_operation_get_state(op)!=PA_OPERATION_RUNNING; },3000,"Playback drain timed out");
        } catch (...) { pa_operation_cancel(op); pa_operation_unref(op); throw; }
        pa_operation_unref(op);
        if (result!=1) throw std::runtime_error("Playback drain failed");
        finished_=true;
    }
    void cancel() noexcept {
        if (!running_ || !stream_) return;
        Lock lock(loop_);
        if (pa_stream_get_state(stream_)!=PA_STREAM_READY) return;
        auto* cork=pa_stream_cork(stream_,1,nullptr,nullptr);
        if (cork) pa_operation_unref(cork);
        auto* op=pa_stream_flush(stream_,nullptr,nullptr);
        if (!op) return;
        const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        while (pa_operation_get_state(op)==PA_OPERATION_RUNNING && std::chrono::steady_clock::now()<end) pause();
        if (pa_operation_get_state(op)==PA_OPERATION_RUNNING) pa_operation_cancel(op);
        pa_operation_unref(op);
    }
};
}
