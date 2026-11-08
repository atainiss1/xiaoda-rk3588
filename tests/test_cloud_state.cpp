#define RKVOICE_CLOUD_TEST
#include "../src/cloud_main.cpp"
#include "../src/wake_match.hpp"

namespace {
struct CloudTest {
    static void feed(Cloud& c,const std::string& value,bool binary=false,ix::WebSocketMessageType type=ix::WebSocketMessageType::Message) {
        c.message(std::make_unique<ix::WebSocketMessage>(type,value,value.size(),ix::WebSocketErrorInfo{},ix::WebSocketOpenInfo{},ix::WebSocketCloseInfo{},binary));
    }
    static void run(const char* catalog_path = nullptr) {
        const float greeting[]={1.16f,1.20f,1.36f,1.40f,1.56f,1.64f,1.76f,1.80f};
        const float stitched[]={1.12f,1.16f,1.36f,1.44f,2.32f,2.60f,2.72f,2.76f};
        const float invalid[]={1,1.1f,1.2f,1.3f,1.4f,1.5f,1.4f,1.7f};
        require(rkvoice::compact_wake_phrase(greeting,8),"Compact greeting rejected");
        require(!rkvoice::compact_wake_phrase(stitched,8),"Separated greeting/name was treated as a wake phrase");
        require(!rkvoice::compact_wake_phrase(invalid,8) && !rkvoice::compact_wake_phrase(greeting,7),"Invalid wake alignment accepted");
        Cloud c;
        feed(c,"",false,ix::WebSocketMessageType::Open);
        feed(c,R"({"type":"hello","transport":"websocket","session_id":"synthetic","audio_params":{"format":"opus","channels":1,"sample_rate":16000}})");
        require(c.connected(),"Synthetic connection unavailable");
        feed(c,R"({"type":"mcp","payload":{"jsonrpc":"2.0","id":900,"method":42}})");
        feed(c,R"({"type":"mcp","payload":"invalid payload"})");
        require(c.failure_.empty(),"Malformed product tool request poisoned the voice session");
        auto encoder=make_encoder();
        std::array<opus_int16,960> silence{}; std::array<unsigned char,4000> packet{};
        const int size=opus_encode(encoder.get(),silence.data(),960,packet.data(),4000);
        require(size>0,"Synthetic encoder failed");
        const std::string audioPacket(reinterpret_cast<char*>(packet.data()),static_cast<std::size_t>(size));
        for (int i=0;i<5;++i) {
            c.begin_turn();
            feed(c,audioPacket,true); require(c.reply_.empty(),"Accepted audio before TTS start");
            feed(c,R"({"type":"tts","state":"start"})"); feed(c,audioPacket,true);
            require(c.reply_.size()==1 && c.packets_==1,"New turn audio missing");
            feed(c,R"({"type":"tts","state":"stop"})");
            feed(c,audioPacket,true); require(c.reply_.size()==1,"Late completed audio was accepted");
            require(c.connected() && c.done_,"Normal turn destroyed session");
        }
        c.begin_turn();
        feed(c,R"({"type":"stt","text":"介绍下cads产品。"})");
        require(c.products_.language()=="zh","Chinese STT changed product language");
        feed(c,R"({"type":"stt","text":"请用英文介绍。"})");
        require(c.products_.language()=="en","Explicit STT language request not applied");
        feed(c,R"({"type":"tts","state":"start"})");
        const auto before=c.response_activity_;
        c.check_response_timeout(before+19s);
        bool timedOut=false;
        try { c.check_response_timeout(before+20s); } catch (const CloudResponseTimeout&) { timedOut=true; }
        require(timedOut,"Cloud with no response waited beyond deadline");
        c.response_activity_=before-30s;
        feed(c,R"({"type":"stt","text":"synthetic progress"})");
        c.check_response_timeout(std::chrono::steady_clock::now());
        feed(c,audioPacket,true);
        c.check_response_timeout(c.response_activity_+30s); // queued playable audio is not a stall
        c.reply_.clear();
        c.aborting_=true; c.accepting_=false; c.reply_.clear(); c.abort_ack_=false;
        feed(c,audioPacket,true);
        feed(c,R"({"type":"tts","state":"sentence_start","text":"OLD RESPONSE"})");
        feed(c,R"({"type":"stt","text":"切回中文。"})");
        require(c.products_.language()=="en","Aborted STT changed language");
        require(c.reply_.empty() && c.sentences_==0,"Aborted data leaked");
        bool refused=false; try { c.begin_turn(); } catch (...) { refused=true; }
        require(refused,"Reused session before abort barrier");
        feed(c,R"({"type":"tts","state":"stop"})");
        require(c.abort_ack_ && c.done_,"Abort stop barrier missing");
        c.aborting_=false; c.begin_turn();
        feed(c,R"({"type":"tts","state":"start"})"); feed(c,audioPacket,true);
        require(c.reply_.size()==1,"New audio lost after interruption");
        feed(c,R"({"type":"tts","state":"stop"})");
        feed(c,"",false,ix::WebSocketMessageType::Close);
        require(!c.connected() && c.failure_.empty(),"Idle close not distinguished from turn failure");
        refused=false; try { c.begin_turn(); } catch (...) { refused=true; }
        require(refused,"Reused closed connection");

        // Replay actual STT through Cloud, then retain the corrected preference across a new connection.
        auto preference=std::make_shared<rkvoice::ConversationLanguage>();
        Cloud replay(preference);
        feed(replay,"",false,ix::WebSocketMessageType::Open);
        feed(replay,R"({"type":"hello","transport":"websocket","session_id":"replay","audio_params":{"format":"opus","channels":1,"sample_rate":16000}})");
        replay.begin_turn();
        feed(replay,R"({"type":"stt","text":"可以用英文介绍吗？"})");
        require(preference->value()=="en","Recorded English switch failed");
        feed(replay,R"({"type":"stt","text":"还是用中文一句话跟我说吧。"})");
        require(preference->value()=="zh","Recorded Chinese switch was missed");
        Cloud next(preference);
        if (catalog_path) next.products_.load(catalog_path);
        feed(next,"",false,ix::WebSocketMessageType::Open);
        feed(next,R"({"type":"hello","transport":"websocket","session_id":"next","audio_params":{"format":"opus","channels":1,"sample_rate":16000}})");
        next.begin_turn();
        feed(next,R"({"type":"stt","text":"那先不出门了，你给我介绍一下cas的产品。"})");
        require(next.products_.language()=="zh","Later brand request resurrected stale English");
        json displayed;
        auto answer=next.products_.handle({{"jsonrpc","2.0"},{"id",8123},{"method","tools/call"},{"params",{{"name","self.khadas.present"},{"arguments",{{"action","show"},{"product","overview"},{"language","en"}}}}}},true,[&](const json& x){displayed=x;});
        require(displayed.value("language","")=="zh","Product board did not use corrected Chinese preference");
        auto facts=json::parse(answer["result"]["content"][0]["text"].get<std::string>());
        require(facts["reply_language"]=="zh","Tool result still instructed English speech");
        std::cout<<"RECORDED_LANGUAGE_REPLAY_OK english_then_chinese_modifier next_connection product_board_and_reply_zh\n";
        audio::session_cancelled=true; audio::turn_interrupted=true;
        audio::turn_interrupted=false;
        require(audio::cancelled(),"Wake recovery erased user stop");
        audio::session_cancelled=false;
        std::cout << "CLOUD_SESSION_STATE_TEST_OK reuse=5 late_audio=rejected abort_barrier=required stop_priority=true\n";
    }
    static void live() {
        // Explicit manual diagnostic only: uses the existing identity and the
        // caller-selected playback sink. Never run by CTest.
        auto config=Config::load(); config.refresh(); Cloud c; c.connect(config);
        const char* prompts=std::getenv("RKVOICE_TEST_PROMPTS");
        require(prompts && *prompts,"Set RKVOICE_TEST_PROMPTS to synthetic 16kHz mono WAV fixtures");
        const auto upload=[&](int index) {
            const auto file=std::string(prompts)+"/prompt"+std::to_string(index)+".wav";
            const auto wave=audio::load_wave(file.c_str());
            require(wave.sample_rate==16000 && wave.channels==1,"Test WAV must be mono 16kHz");
            c.begin_turn(); auto encoder=make_encoder();
            c.send({{"session_id",c.session_},{"type","listen"},{"state","start"},{"mode","manual"}});
            std::array<opus_int16,960> samples{}; std::array<unsigned char,4000> packet{};
            for (std::size_t offset=0;offset<wave.pcm.size();offset+=1920) {
                samples.fill(0);
                const auto count=std::min(std::size_t{1920},wave.pcm.size()-offset);
                for (std::size_t i=0;i<count/2;++i) samples[i]=static_cast<opus_int16>(rkvoice::decode16(wave.pcm.data()+offset+i*2));
                const int size=opus_encode(encoder.get(),samples.data(),960,packet.data(),4000);
                require(size>0,"Test Opus encoding failed"); c.send_audio(packet.data(),size);
                std::this_thread::sleep_for(20ms);
            }
            c.send({{"session_id",c.session_},{"type","listen"},{"state","stop"}});
            std::cout << "TEST_UTTERANCE index=" << index << " source=local_synthetic_wav" << std::endl;
        };
        upload(0); c.play_response();
        require(c.connected(),"Server closed after first answer");
        upload(1); c.play_response();
        require(c.connected(),"Server closed after recall answer");
        upload(2);
        std::thread interrupt([&] {
            std::unique_lock<std::mutex> lock(c.mutex_);
            c.changed_.wait_for(lock,15s,[&]{return c.packets_>=5 || !c.failure_.empty();});
            audio::turn_interrupted=true;
        });
        try { c.play_response(); } catch (...) { interrupt.join(); goto interrupted; }
        interrupt.join();
        throw std::runtime_error("Long answer was not interrupted");
interrupted:
        require(audio::turn_interrupted,"Unexpected playback failure");
        const bool retained=c.abort_reply(); audio::turn_interrupted=false;
        require(retained,"Live server did not confirm abort barrier");
        upload(3); c.play_response();
        std::cout << "LIVE_SESSION_TEST_DONE single_connection=true abort_barrier=true review_recall_text=true\n";
    }
    static void stalled_playback() {
        require(std::getenv("RKVOICE_TEST_PLAYBACK")!=nullptr,"Explicit playback test flag required");
        Cloud c;
        feed(c,"",false,ix::WebSocketMessageType::Open);
        feed(c,R"({"type":"hello","transport":"websocket","session_id":"synthetic-stall","audio_params":{"format":"opus","channels":1,"sample_rate":24000}})");
        c.begin_turn();
        const auto start=std::chrono::steady_clock::now();
        bool timeout=false;
        try { c.play_response(); } catch (const CloudResponseTimeout&) { timeout=true; }
        const auto elapsed=std::chrono::steady_clock::now()-start;
        require(timeout && elapsed>=20s && elapsed<23s,"Silent cloud did not leave playback wait within bound");
        require(!audio::session_stopping(),"Timeout cancelled whole chat");
        std::cout << "CLOUD_STALL_TEST_OK bounded_wait=true session_stop=false\n";
    }
};
}
int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="--live-session") CloudTest::live();
        else if (argc==2 && std::string(argv[1])=="--stall-playback") CloudTest::stalled_playback();
        else if (argc==3 && std::string(argv[1])=="--catalog") CloudTest::run(argv[2]);
        else if (argc==1) CloudTest::run();
        else return 2;
        return 0;
    } catch (const std::exception& e) { std::cerr << "SESSION_TEST_FAILED " << e.what() << '\n'; return 1; }
}
