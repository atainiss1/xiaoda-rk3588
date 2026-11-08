#include "audio.hpp"
#include "live_playback.hpp"
#include "vad.hpp"
#include "stream_pcm.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXHttpClient.h>
#include <nlohmann/json.hpp>
#include <opus.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <filesystem>
#include "product_mcp.hpp"
#include <poll.h>
#ifdef RKVOICE_SPEECH
#include "voice_frontend.hpp"
#endif

using json = nlohmann::json;
using namespace std::chrono_literals;
namespace audio = rkvoice::audio;
namespace {
const auto process_started = std::chrono::steady_clock::now();
long long elapsed_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - process_started).count();
}
volatile std::sig_atomic_t finish_recording = 0;
void finish_turn(int) { finish_recording = 1; }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
json read_json(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    require(in && in.tellg() > 0 && in.tellg() <= 1024 * 1024, "Missing/oversized device configuration");
    in.seekg(0);
    // Parse errors may include input fragments: never expose credential bytes.
    try { return json::parse(in); } catch (...) { throw std::runtime_error("Invalid device configuration JSON"); }
}
std::string required_string(const json& object, const char* key) {
    require(object.contains(key) && object.at(key).is_string(), "Missing required configuration field");
    auto value = object.at(key).get<std::string>();
    require(!value.empty() && value.size() <= 8192 && value.find_first_of("\r\n") == std::string::npos,
            "Invalid configuration field");
    return value;
}
ix::SocketTLSOptions tls_options() {
    ix::SocketTLSOptions tls;
    tls.caFile = "/etc/ssl/certs/ca-certificates.crt";
    return tls; // Both certificate and hostname verification remain enabled.
}
struct Config {
    std::string device, client, url, token, ota, activation, hmac;
    static Config load() {
        const char* home = std::getenv("HOME");
        require(home != nullptr, "HOME is missing");
        const char* override_dir = std::getenv("RKVOICE_IDENTITY_DIR");
        const std::string dir = override_dir ? override_dir : std::string(home) + "/.local/share/py-xiaozhi/config";
        auto config = read_json(dir + "/config.json");
        auto efuse = read_json(dir + "/efuse.json");
        const auto& system = config.at("SYSTEM_OPTIONS");
        const auto& network = system.at("NETWORK");
        Config result;
        result.device = required_string(system, "DEVICE_ID");
        result.client = required_string(system, "CLIENT_ID");
        result.ota = required_string(network, "OTA_VERSION_URL");
        result.activation = network.value("ACTIVATION_VERSION", "v1");
        result.hmac = required_string(efuse, "hmac_key");
        require(result.ota.rfind("https://", 0) == 0, "OTA must use verified HTTPS");
        std::cout << "IDENTITY_LOADED source=existing-bound-device (read-only)" << std::endl;
        return result;
    }
    void refresh() {
        ix::HttpClient http;
        http.setTLSOptions(tls_options());
        auto args = http.createRequest();
        args->connectTimeout = 10;
        args->transferTimeout = 20;
        args->followRedirects = false;
        args->compress = false;
        args->extraHeaders = {{"Device-Id", device}, {"Client-Id", client},
            {"Content-Type", "application/json"}, {"Accept-Language", "zh-CN"},
            {"User-Agent", "bread-compact-wifi/py-xiaozhi-2.1.2"}};
        if (activation == "v2") args->extraHeaders["Activation-Version"] = "2.1.2";
        // Preserve the registered device's OTA metadata. This is a native C++ request.
        json body = {{"application", {{"version", "2.1.2"}, {"elf_sha256", hmac}}},
                     {"board", {{"type", "bread-compact-wifi"}, {"name", "py-xiaozhi"}, {"mac", device}}}};
        auto response = http.post(ota, body.dump(), args);
        if (response->errorCode != ix::HttpErrorCode::Ok || response->statusCode != 200)
            throw std::runtime_error("OTA request failed: HTTP=" + std::to_string(response->statusCode) +
                                     " network_code=" + std::to_string(static_cast<int>(response->errorCode)));
        require(response->body.size() <= 1024 * 1024, "OTA response too large");
        const auto data = json::parse(response->body, nullptr, false);
        require(data.is_object(), "Invalid OTA response");
        require(!data.contains("activation"), "Server requested activation; original identity preserved, no rebind attempted");
        require(data.contains("websocket"), "OTA did not provide a WebSocket endpoint");
        url = required_string(data.at("websocket"), "url");
        token = required_string(data.at("websocket"), "token");
        require(url.rfind("wss://", 0) == 0, "WebSocket must use verified TLS");
        std::cout << "OTA_AUTHORIZED existing_device=true tls_verified=true" << std::endl;
    }
};
using Encoder = std::unique_ptr<OpusEncoder, decltype(&opus_encoder_destroy)>;
using Decoder = std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)>;
Encoder make_encoder() {
    int error = 0;
    Encoder encoder(opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error), opus_encoder_destroy);
    require(encoder && error == OPUS_OK, "Opus encoder creation failed");
    require(opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(24000)) == OPUS_OK, "Opus bitrate failed");
    return encoder;
}
int server_rate(const json& hello) {
    const auto& p = hello.at("audio_params");
    require(p.value("format", "") == "opus" && p.value("channels", 0) == 1, "Unsupported server audio format");
    const int rate = p.value("sample_rate", 0);
    require(rate == 8000 || rate == 12000 || rate == 16000 || rate == 24000 || rate == 48000,
            "Unsupported server Opus sample rate");
    return rate;
}
class CloudResponseTimeout : public std::runtime_error {
public:
    CloudResponseTimeout():std::runtime_error("Cloud response inactive for 20 seconds") {}
};
class Cloud {
#ifdef RKVOICE_CLOUD_TEST
    friend struct CloudTest;
#endif
    // Socket is stopped in the destructor before any callback state is destroyed.
    ix::WebSocket ws_;
    rkvoice::ProductMcp products_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_ = false, hello_ = false, done_ = false;
    bool accepting_ = false, tts_started_ = false, aborting_ = false, abort_ack_ = false;
    std::string failure_, session_;
    int rate_ = 0;
    Decoder decoder_{nullptr, opus_decoder_destroy};
    std::deque<std::vector<unsigned char>> reply_;
    std::size_t received_bytes_ = 0;
    std::size_t packets_ = 0;
    std::size_t sentences_ = 0;
    std::chrono::steady_clock::time_point response_activity_=std::chrono::steady_clock::now();
    void check_response_timeout(std::chrono::steady_clock::time_point now) const {
        if (!done_ && reply_.empty() && now-response_activity_>=20s) throw CloudResponseTimeout();
    }
    void message(const ix::WebSocketMessagePtr& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            if (message->type == ix::WebSocketMessageType::Open) open_ = true;
            else if (message->type == ix::WebSocketMessageType::Error) {
                open_=false;
                failure_ = "WebSocket connection error (HTTP " + std::to_string(message->errorInfo.http_status) + ")";
            }
            else if (message->type == ix::WebSocketMessageType::Close) {
                open_=false;
                std::cout << "CLOUD_CLOSED code=" << message->closeInfo.code << " active=" << accepting_
                          << " t_ms=" << elapsed_ms() << std::endl;
                if (accepting_ && !done_ && !aborting_) failure_ = "WebSocket closed before turn completion";
            } else if (message->type == ix::WebSocketMessageType::Message) {
                if (message->binary) {
                    if (!accepting_ || aborting_ || !tts_started_) return;
                    require(hello_ && decoder_, "Audio received before server hello");
                    require(message->str.size() <= 65536, "Opus packet too large");
                    std::vector<opus_int16> pcm(static_cast<std::size_t>(rate_) * 120 / 1000);
                    const int frames = opus_decode(decoder_.get(), reinterpret_cast<const unsigned char*>(message->str.data()),
                        static_cast<opus_int32>(message->str.size()), pcm.data(), static_cast<int>(pcm.size()), 0);
                    require(frames > 0, "Opus decode failed");
                    const auto byte_count = static_cast<std::size_t>(frames) * 2;
                    require(received_bytes_ + byte_count <= static_cast<std::size_t>(rate_) * 2 * 120,
                            "Reply exceeded 120-second audio buffer");
                    std::vector<unsigned char> decoded(byte_count);
                    for (int i = 0; i < frames; ++i) {
                        const auto sample = static_cast<std::uint16_t>(pcm[static_cast<std::size_t>(i)]);
                        rkvoice::put16(decoded.data() + static_cast<std::size_t>(i) * 2, sample);
                    }
                    reply_.push_back(std::move(decoded));
                    received_bytes_ += byte_count;
                    if (packets_ == 0) std::cout << "TTS_FIRST_AUDIO t_ms=" << elapsed_ms() << std::endl;
                    ++packets_;
                    response_activity_=std::chrono::steady_clock::now();
                } else {
                    require(message->str.size() <= 65536, "Server JSON too large");
                    const auto data = json::parse(message->str, nullptr, false);
                    require(data.is_object(), "Invalid server JSON");
                    const auto type = data.value("type", "");
                    std::cout << "CLOUD_EVENT type=" << type << " state=" << data.value("state", "") << std::endl;
                    if (type == "mcp") {
                        // Protocol failures stay local to this tool, never poison voice playback.
                        const auto payload=data.value("payload",json::object());
                        const auto response=products_.handle(payload,
                            accepting_ && !aborting_ && !audio::cancelled(), [](const json& event) {
                                std::cout << ("PRODUCT_VIEW "+event.dump()+"\n") << std::flush;
                            });
                        if (!response.is_null()) {
                            const auto envelope=json{{"session_id",session_},{"type","mcp"},{"payload",response}};
                            ws_.sendText(envelope.dump());
                            if(payload.is_object() && payload.contains("method") && payload.at("method")=="tools/list")
                                std::cout << "PRODUCT_TOOLS registered=" << products_.ready() << std::endl;
                        }
                        if(accepting_) response_activity_=std::chrono::steady_clock::now();
                        return;
                    }
                    // The ordered TTS stop is the barrier before reusing an aborted
                    // session. Until then, discard both old subtitles and audio.
                    if (aborting_) {
                        if (type=="tts" && data.value("state","")=="stop") {
                            abort_ack_=true; done_=true; changed_.notify_all();
                        }
                        return;
                    }
                    if (type == "hello") {
                        require(!hello_ && data.value("transport", "") == "websocket", "Invalid server hello");
                        rate_ = server_rate(data);
                        session_ = required_string(data, "session_id");
                        int error = 0;
                        decoder_.reset(opus_decoder_create(rate_, 1, &error));
                        require(decoder_ && error == OPUS_OK, "Opus decoder creation failed");
                        hello_ = true;
                        std::cout << "CLOUD_READY opus_receive_rate=" << rate_ << " tls_verified=true" << std::endl;
                    } else if (type == "stt") {
                        if (accepting_) {
                            response_activity_=std::chrono::steady_clock::now();
                            std::cout << "STT " << data.value("text", "") << std::endl;
                            if(!audio::cancelled())products_.observe_language(data.value("text", ""));
                        }
                    } else if (type == "tts") {
                        if (!accepting_) return;
                        const auto state = data.value("state", "");
                        response_activity_=std::chrono::steady_clock::now();
                        if (state == "start") tts_started_=true;
                        if (state == "sentence_start") {
                            ++sentences_;
                            std::cout << "TTS_SENTENCE index=" << sentences_ << " t_ms=" << elapsed_ms() << std::endl;
                            std::cout << "ASSISTANT " << data.value("text", "") << std::endl;
                        }
                        if (state == "stop") { done_ = true; accepting_=false; std::cout << "TTS_RECEIVED packets=" << packets_
                            << " sentences=" << sentences_ << " t_ms=" << elapsed_ms() << std::endl; }
                    } else if (type == "error" || type == "alert") {
                        std::cout << "CLOUD_NOTICE " << data.value("message", "") << std::endl;
                        failure_ = "Server reported a conversation error";
                    }
                }
            }
        } catch (...) { failure_ = "Invalid cloud message or audio decode failure"; }
        changed_.notify_all();
    }
    void wait_for(bool& flag, int seconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        std::unique_lock<std::mutex> lock(mutex_);
        while (!flag && failure_.empty() && !audio::cancelled() && std::chrono::steady_clock::now() < end)
            changed_.wait_for(lock, 200ms);
        if (!failure_.empty()) throw std::runtime_error(failure_);
        require(!audio::cancelled(), "Cancelled");
        require(flag, "Timed out waiting for cloud response");
    }
public:
    explicit Cloud(std::shared_ptr<rkvoice::ConversationLanguage> language=std::make_shared<rkvoice::ConversationLanguage>()) : products_(json::object(),std::move(language)) {
        std::error_code ec;
        const auto exe=std::filesystem::read_symlink("/proc/self/exe",ec);
        if(!ec)products_.load((exe.parent_path().parent_path()/"data/khadas-products.json").string());
    }
    ~Cloud() { ws_.stop(); }
    bool connected() {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_ && hello_ && failure_.empty();
    }
    bool abort_reply(const char* reason="wake_word_detected") {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_=false; aborting_=true; abort_ack_=done_; reply_.clear();
            if (!open_ || !hello_ || !failure_.empty()) return false;
            if (done_) {
                aborting_=false;
                std::cout << "ABORT_BARRIER safe=1 server_already_finished=true t_ms=" << elapsed_ms() << std::endl;
                return true;
            }
        }
        // Stop and shutdown take priority. A wake interruption alone must still
        // be able to send abort, without resetting the whole chat session.
        if (audio::session_stopping()) return false;
        json request={{"session_id",session_},{"type","abort"}};
        if (std::string(reason)=="wake_word_detected") request["reason"]=reason;
        if (!ws_.sendText(request.dump()).success) return false;
        std::cout << "ABORT_SENT reason=" << reason << " t_ms=" << elapsed_ms() << std::endl;
        const auto end=std::chrono::steady_clock::now()+2s;
        std::unique_lock<std::mutex> lock(mutex_);
        while (!abort_ack_ && open_ && failure_.empty() && !audio::session_stopping() && std::chrono::steady_clock::now()<end)
            changed_.wait_for(lock,20ms);
        const bool safe=abort_ack_ && open_ && failure_.empty() && !audio::session_stopping();
        if (safe) aborting_=false;
        std::cout << "ABORT_BARRIER safe=" << safe << " t_ms=" << elapsed_ms() << std::endl;
        return safe;
    }
    void connect(const Config& config) {
        ws_.setUrl(config.url);
        ws_.setTLSOptions(tls_options());
        ws_.setExtraHeaders({{"Authorization", "Bearer " + config.token}, {"Protocol-Version", "1"},
                             {"Device-Id", config.device}, {"Client-Id", config.client}});
        ws_.disablePerMessageDeflate();
        ws_.disableAutomaticReconnection();
        ws_.setHandshakeTimeout(10);
        ws_.setPingInterval(20);
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { message(msg); });
        ws_.start();
        wait_for(open_, 15);
        send({{"type", "hello"}, {"version", 1}, {"transport", "websocket"},
              {"features", {{"mcp", products_.ready()}}},
              {"audio_params", {{"format", "opus"}, {"sample_rate", 16000}, {"channels", 1}, {"frame_duration", 60}}}});
        wait_for(hello_, 15);
    }
    void send(const json& data) {
        require(!audio::cancelled(), "Cancelled");
        audio::Deadline deadline;
        require(ws_.sendText(data.dump()).success, "WebSocket text send failed");
    }
    void send_audio(const unsigned char* bytes, int size) {
        require(!audio::cancelled(), "Cancelled");
        audio::Deadline deadline;
        require(ws_.sendBinary(std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size))).success,
                "WebSocket audio send failed");
    }
    void begin_turn() {
        std::lock_guard<std::mutex> lock(mutex_);
        require(open_ && hello_ && failure_.empty(), "Cloud connection lost");
        require(!aborting_ && !accepting_, "Previous cloud turn is still active");
        reply_.clear(); received_bytes_ = 0; packets_ = 0; sentences_ = 0; done_ = false;
        accepting_=true; tts_started_=false; abort_ack_=false;
        require(opus_decoder_ctl(decoder_.get(), OPUS_RESET_STATE) == OPUS_OK, "Decoder reset failed");
    }
    void text(const std::string& text) {
        begin_turn();
        // The hosted server ignores detect while idle. Establish a listening
        // session first, matching the registered client's session flow.
        send({{"session_id", session_}, {"type", "listen"}, {"state", "start"}, {"mode", "manual"}});
        send({{"session_id", session_}, {"type", "listen"}, {"state", "detect"}, {"text", text}});
        std::cout << "LISTEN_STOP t_ms=" << elapsed_ms() << std::endl;
        send({{"session_id", session_}, {"type", "listen"}, {"state", "stop"}});
    }
    bool capture(int seconds, bool automatic) {
        begin_turn();
        finish_recording = 0;
        auto encoder = make_encoder();
        rkvoice::vad::Detector detector;
        rkvoice::vad::Endpointer endpoint(seconds);
        rkvoice::vad::PreRoll pre_roll;
        bool transmitting = !automatic;
        auto start_listening = [&] {
            send({{"session_id", session_}, {"type", "listen"}, {"state", "start"}, {"mode", "manual"}});
        };
        std::cout << "PREPARING_CAPTURE wait_for_RECORD_READY" << std::endl;
        auto stream = audio::connect(true, 16000, 2);
        std::vector<double> noise_calibration;
        if (automatic) {
            // Let the ADC settle and let WebRTC learn the initial noise floor.
            // These samples do not enter the endpoint window or pre-roll.
            std::array<unsigned char, 3840> warmup{};
            rkvoice::vad::Block warm_mono{};
            std::uint64_t warm_clipped = 0;
            for (int i = 0; i < 20 && !audio::stopped; ++i) {
                audio::read_audio(stream.get(), warmup.data(), warmup.size());
                for (std::size_t sample = 0; sample < warm_mono.size(); ++sample)
                    warm_mono[sample] = static_cast<opus_int16>(rkvoice::amplify(
                        rkvoice::decode16(warmup.data() + sample * 4), 4, warm_clipped));
                detector.speech(warm_mono);
                if (i >= 5) noise_calibration.push_back(rkvoice::vad::rms(warm_mono));
            }
        } else {
            std::array<unsigned char, 640> warmup{};
            for (int i = 0; i < 50 && !audio::stopped; ++i)
                audio::read_audio(stream.get(), warmup.data(), warmup.size());
        }
        rkvoice::vad::NoiseGuard noise_guard(std::move(noise_calibration));
        if (audio::stopped) return false;
        if (transmitting) start_listening();
        std::cout << "RECORD_READY mode=" << (automatic ? "vad" : "manual")
                  << " max_speech_seconds=" << seconds
                  << " (Enter or SIGUSR1 ends this turn)" << std::endl;
        if (automatic)
            std::cout << "VAD_CONFIG engine=libfvad mode=2 wait_seconds=60 preroll_ms=600 silence_ms=1200 onset_frames=5/8"
                      << " noise_rms=" << noise_guard.noise() << " onset_rms=" << noise_guard.onset()
                      << " continuation_rms=" << noise_guard.continuation() << std::endl;
        std::array<unsigned char, 3840> stereo{};
        rkvoice::vad::Block mono{};
        std::array<unsigned char, 4000> packet{};
        std::uint64_t clipped = 0;
        int captured = 0, sent = 0, voiced = 0;
        const char* reason = "manual";
        auto upload = [&](const rkvoice::vad::Block& block) {
            const int size = opus_encode(encoder.get(), block.data(), 960, packet.data(), static_cast<opus_int32>(packet.size()));
            require(size > 0, "Opus encode failed");
            send_audio(packet.data(), size);
            ++sent;
        };
        while (!finish_recording && !audio::stopped) {
            if (::isatty(STDIN_FILENO)) {
                pollfd input{STDIN_FILENO, POLLIN, 0};
                if (::poll(&input, 1, 0) > 0 && (input.revents & POLLIN)) {
                    char line[256]; const auto ignored = ::read(STDIN_FILENO, line, sizeof(line)); (void)ignored;
                    break;
                }
            }
            audio::read_audio(stream.get(), stereo.data(), stereo.size());
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] = static_cast<opus_int16>(rkvoice::amplify(rkvoice::decode16(stereo.data() + i * 4), 4, clipped));
            ++captured;
            if (audio::stopped) break;
            { std::lock_guard<std::mutex> lock(mutex_); if (!failure_.empty()) throw std::runtime_error(failure_); }
            if (automatic) {
                const bool speech = noise_guard.speech(detector.speech(mono), mono, transmitting);
                voiced += speech ? 1 : 0;
                const auto event = endpoint.push(speech);
                if (!transmitting) pre_roll.push(mono);
                if (event == rkvoice::vad::Event::started) {
                    start_listening();
                    transmitting = true;
                    std::cout << "SPEECH_START captured_ms=" << captured * 60 << std::endl;
                    pre_roll.flush(upload);
                } else if (transmitting) {
                    upload(mono);
                }
                if (event == rkvoice::vad::Event::no_speech) { reason = "no_speech"; break; }
                if (event == rkvoice::vad::Event::silence) { reason = "silence"; break; }
                if (event == rkvoice::vad::Event::limit) { reason = "max_duration"; break; }
            } else {
                upload(mono);
                if (captured * 60 >= seconds * 1000) { reason = "max_duration"; break; }
            }
        }
        stream.reset(); // Half duplex: release capture before speaker playback.
        // Cancellation never submits an unfinished utterance; socket closes on unwind.
        if (audio::stopped) return false;
        std::cout << "CAPTURE_END reason=" << reason << " captured_frames=" << captured
                  << " voiced_frames=" << voiced << " clipped_samples=" << clipped << std::endl;
        if (!transmitting) {
            std::cout << "NO_SPEECH no_audio_submitted=true" << std::endl;
            return false;
        }
        std::cout << "LISTEN_STOP t_ms=" << elapsed_ms() << std::endl;
        send({{"session_id", session_}, {"type", "listen"}, {"state", "stop"}});
        std::cout << "UPLOAD_DONE frames=" << sent << std::endl;
        require(sent > 0, "No audio captured");
        return true;
    }
#ifdef RKVOICE_SPEECH
    void capture_live(rkvoice::VoiceFrontend& front) {
        begin_turn();
        auto encoder=make_encoder();
        send({{"session_id",session_},{"type","listen"},{"state","start"},{"mode","manual"}});
        int sent=0;
        std::array<unsigned char,4000> packet{};
        while (!audio::cancelled()) {
            rkvoice::VoiceFrontend::Event event;
            if (!front.next(event)) continue;
            if (event.type==rkvoice::VoiceFrontend::Event::end) break;
            if (event.type!=rkvoice::VoiceFrontend::Event::pcm) continue;
            const int size=opus_encode(encoder.get(),event.samples.data(),960,packet.data(),static_cast<opus_int32>(packet.size()));
            require(size>0,"Opus encode failed"); send_audio(packet.data(),size); ++sent;
        }
        require(!audio::cancelled(),"Cancelled");
        require(sent>0,"No audio captured");
        std::cout << "LISTEN_STOP t_ms=" << elapsed_ms() << std::endl;
        send({{"session_id",session_},{"type","listen"},{"state","stop"}});
        std::cout << "UPLOAD_DONE frames=" << sent << std::endl;
    }
#endif
    void play_response() {
        int rate;
        { std::lock_guard<std::mutex> lock(mutex_); rate = rate_; response_activity_=std::chrono::steady_clock::now(); }
        std::cout << "PLAYBACK_OPEN_BEGIN t_ms=" << elapsed_ms() << std::endl;
        audio::LivePlayback stream(static_cast<std::uint32_t>(rate));
        std::cout << "PLAYBACK_OPEN_DONE t_ms=" << elapsed_ms() << std::endl;
        auto write = [&](const std::vector<unsigned char>& bytes) {
            if (bytes.empty()) return;
            stream.write(bytes);
        };
        // Prepare the amplifier while the server is still generating its reply.
        std::vector<unsigned char> quiet(static_cast<std::size_t>(rate) * 2 / 100, 0);
        for (int i = 0; i < 30; ++i) write(quiet);
        std::cout << "PLAYBACK_WARMUP_DONE t_ms=" << elapsed_ms() << std::endl;
        rkvoice::StreamEnvelope envelope(rate);
        bool started = false;
        std::size_t played = 0;
        while (true) {
            std::vector<unsigned char> packet;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                while (reply_.empty() && !done_ && failure_.empty() && !audio::cancelled()) {
                    check_response_timeout(std::chrono::steady_clock::now());
                    changed_.wait_for(lock, 100ms);
                }
                if (!failure_.empty()) throw std::runtime_error(failure_);
                if (audio::cancelled()) throw std::runtime_error("Playback cancelled");
                if (reply_.empty()) {
                    require(done_, "Timed out waiting for cloud audio");
                    break;
                }
                packet = std::move(reply_.front());
                reply_.pop_front();
            } // Never block on the audio device while holding the callback mutex.
            auto output = envelope.push(packet);
            played += packet.size();
            if (!started && !output.empty()) {
                started = true;
                std::cout << "PLAYBACK_START streaming=true t_ms=" << elapsed_ms() << std::endl;
            }
            write(output);
        }
        require(played > 0, "Cloud returned no speech audio");
        write(envelope.finish());
        for (int i = 0; i < 10; ++i) write(quiet);
        stream.drain();
        require(!audio::cancelled(), "Playback cancelled");
        std::cout << "DIALOGUE_DONE seconds=" << static_cast<double>(played) / (rate * 2)
                  << " t_ms=" << elapsed_ms() << std::endl;
    }
};
#include "voice_service.hpp"
void self_test() {
    auto encoder = make_encoder();
    std::array<opus_int16, 960> pcm{};
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<opus_int16>(6000 * std::sin(2 * 3.141592653589793 * 440 * static_cast<double>(i) / 16000));
    std::array<unsigned char, 4000> packet{};
    const int size = opus_encode(encoder.get(), pcm.data(), 960, packet.data(), static_cast<opus_int32>(packet.size()));
    require(size > 0 && opus_packet_get_nb_samples(packet.data(), size, 16000) == 960, "Codec packet framing test failed");
    int error = 0;
    Decoder decoder(opus_decoder_create(16000, 1, &error), opus_decoder_destroy);
    require(decoder && error == OPUS_OK, "Test decoder creation failed");
    require(opus_decode(decoder.get(), packet.data(), size, pcm.data(), 960, 0) == 960, "Codec decode test failed");
    json hello = {{"audio_params", {{"format", "opus"}, {"channels", 1}, {"sample_rate", 24000}}}};
    require(server_rate(hello) == 24000, "Server rate test failed");
    hello["audio_params"]["sample_rate"] = 44100;
    bool rejected = false;
    try { server_rate(hello); } catch (...) { rejected = true; }
    require(rejected, "Invalid server rate accepted");
    rejected = false;
    try { required_string(json{{"token", "bad\r\nHeader: injected"}}, "token"); } catch (...) { rejected = true; }
    require(rejected, "Header injection accepted");
    rkvoice::vad::Detector detector;
    rkvoice::vad::Block silence{};
    require(!detector.speech(silence), "VAD silence test failed");
    std::cout << "CLOUD_CODEC_PROTOCOL_TEST_OK" << std::endl;
}
} // namespace
#ifndef RKVOICE_CLOUD_TEST
int main(int argc, char** argv) {
    std::signal(SIGINT, audio::on_stop);
    std::signal(SIGTERM, audio::on_stop);
    std::signal(SIGALRM, audio::on_timeout);
    std::signal(SIGUSR1, finish_turn);
    std::signal(SIGPIPE, SIG_IGN);
    int attempted = 0, completed = 0, no_speech = 0;
    const auto summary = [&](const char* status, int code) {
        std::cout << "RUN_SUMMARY status=" << status << " attempted=" << attempted
                  << " completed=" << completed << " no_speech=" << no_speech
                  << " exit_code=" << code << " t_ms=" << elapsed_ms() << std::endl;
        return code;
    };
    try {
        const std::string command = argc > 1 ? argv[1] : "";
#ifdef RKVOICE_SPEECH
        if (argc==2 && command=="service") return voice_service();
#endif
        if (argc == 2 && command == "--self-test") { self_test(); return 0; }
        const bool probe = argc == 2 && command == "probe";
        const bool text = argc == 3 && command == "text";
        const bool talk = (argc == 2 || argc == 3 || argc == 4) && command == "talk";
        const bool automatic = (argc == 2 || argc == 3 || argc == 4) && command == "auto";
        if (!probe && !text && !talk && !automatic) {
            std::cerr << "Usage: rkvoice_cloud probe | text TEXT | talk [SECONDS=30] [TURNS=1] | auto [MAX_SPEECH_SECONDS=30] [TURNS=1] | --self-test\n"
                         "Reads existing bound identity from ~/.local/share/py-xiaozhi/config (never rewrites it).\n";
            return 2;
        }
        const int seconds = (talk || automatic) && argc >= 3 ? rkvoice::number(argv[2], 1, 60) : 30;
        const int turns = (talk || automatic) && argc == 4 ? rkvoice::number(argv[3], 1, 20) : 1;
        auto config = Config::load();
        config.refresh();
        for (int turn = 0; turn < turns && !audio::stopped; ++turn) {
            attempted = turn + 1;
            std::cout << "TURN_BEGIN index=" << attempted << " total=" << turns
                      << " connection=new t_ms=" << elapsed_ms() << std::endl;
            Cloud cloud;
            cloud.connect(config);
            if (probe) { std::cout << "CLOUD_PROBE_OK" << std::endl; return 0; }
            if (text) cloud.text(argv[2]);
            else if (!cloud.capture(seconds, automatic)) {
                if (audio::stopped) break;
                ++no_speech;
                std::cout << "TURN_END index=" << attempted << " status=no_speech t_ms=" << elapsed_ms() << std::endl;
                continue;
            }
            cloud.play_response();
            ++completed;
            std::cout << "TURN_END index=" << attempted << " status=completed t_ms=" << elapsed_ms() << std::endl;
        }
        if (audio::stopped) return summary("cancelled", 130);
        return completed ? summary("completed", 0) : summary("no_speech", 3);
    } catch (const json::exception&) {
        if (audio::stopped) return summary("cancelled", 130);
        std::cerr << "rkvoice_cloud: device configuration structure is invalid\n";
    } catch (const std::exception& error) {
        if (audio::stopped) return summary("cancelled", 130);
        std::cerr << "rkvoice_cloud: " << error.what() << '\n';
    }
    return summary("failed", 1);
}
#endif
