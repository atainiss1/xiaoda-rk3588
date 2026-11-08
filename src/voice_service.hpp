// Included after Cloud's definition. UI talks to this persistent child via stdin.
#ifdef RKVOICE_SPEECH
int voice_service() {
    const char* models=std::getenv("RKVOICE_MODELS");
    require(models && *models,"RKVOICE_MODELS is missing");
    rkvoice::VoiceFrontend front(models);
    auto config=Config::load();
    bool authorized=false;
    std::atomic<bool> start{false}, quit{false}, wakeEnabled{false}, sessionActive{false};
    std::mutex commands;
    std::thread control([&] {
        std::string pending;
        while (!quit && !audio::stopped) {
            pollfd fd{STDIN_FILENO,POLLIN,0};
            if (::poll(&fd,1,100)<=0) continue;
            if (!(fd.revents&(POLLIN|POLLHUP))) continue;
            char bytes[256]; const auto size=::read(STDIN_FILENO,bytes,sizeof(bytes));
            if (size<=0) {
                std::lock_guard<std::mutex> guard(commands);
                quit=true; audio::session_cancelled=true; front.mode(rkvoice::VoiceFrontend::Mode::off); break;
            }
            pending.append(bytes,static_cast<std::size_t>(size));
            if (pending.size()>1024) { quit=true; audio::session_cancelled=true; break; }
            std::size_t end;
            while ((end=pending.find('\n'))!=std::string::npos) {
                auto command=pending.substr(0,end); pending.erase(0,end+1);
                if (!command.empty()&&command.back()=='\r') command.pop_back();
                std::lock_guard<std::mutex> guard(commands);
                if (command=="START") { if (!sessionActive) start=true; }
                else if (command=="STOP" || command=="QUIT") {
                    start=false; wakeEnabled=false; audio::session_cancelled=true;
                    front.takeWake(); front.mode(rkvoice::VoiceFrontend::Mode::off);
                    if (!sessionActive) std::cout << "SESSION_ENDED completed=0 cancelled=1" << std::endl;
                    if (command=="QUIT") quit=true;
                } else if (command=="WAKE_ON") {
                    wakeEnabled=true;
                } else if (command=="WAKE_OFF") {
                    wakeEnabled=false;
                }
            }
        }
    });
    std::cout << "SERVICE_READY engine=silero wakeword=你好小哒 microphone=off" << std::endl;
    bool armed=false;
    auto language=std::make_shared<rkvoice::ConversationLanguage>();
    try {
        while (!quit && !audio::stopped) {
            front.check();
            bool begin=false;
            {
                std::lock_guard<std::mutex> guard(commands);
                const bool woke=front.takeWake();
                begin=start.exchange(false)||woke;
                if (begin) {
                    audio::session_cancelled=false; audio::turn_interrupted=false; sessionActive=true;
                    std::cout << "SESSION_STARTED continuous=true t_ms=" << elapsed_ms() << std::endl;
                }
            }
            if (!begin) {
                if (armed!=wakeEnabled.load()) {
                    armed=wakeEnabled.load();
                    front.mode(armed?rkvoice::VoiceFrontend::Mode::wake:rkvoice::VoiceFrontend::Mode::off);
                    std::cout << "WAKE_ARMED enabled=" << armed << std::endl;
                }
                std::this_thread::sleep_for(50ms); continue;
            }
            int completed=0, attempted=0, interrupted=0, connection=0;
            std::unique_ptr<Cloud> cloud;
            try {
                while (!quit && !audio::session_stopping()) {
                    {
                        std::lock_guard<std::mutex> guard(commands);
                        if (quit || audio::session_stopping()) break;
                        front.mode(rkvoice::VoiceFrontend::Mode::listen);
                    }
                    std::cout << "RECORD_READY mode=continuous engine=silero t_ms=" << elapsed_ms() << std::endl;
                    // Network setup happens only after onset; capture is already running.
                    rkvoice::VoiceFrontend::Event event;
                    bool onset=false;
                    while (!quit && !audio::cancelled()) {
                        if (front.next(event) && event.type==rkvoice::VoiceFrontend::Event::start) { onset=true; break; }
                    }
                    if (!onset || quit || audio::cancelled()) break;
                    const bool reuse=cloud && cloud->connected();
                    ++attempted;
                    std::cout << "TURN_BEGIN index=" << attempted << " total=unlimited connection="
                              << (reuse?"reused":"new") << " t_ms=" << elapsed_ms() << std::endl;
                    try {
                        if (!reuse) {
                            if (cloud) {
                                cloud.reset(); authorized=false;
                                std::cout << "CONTEXT_RESET reason=connection_closed" << std::endl;
                            }
                            if (!authorized) { config.refresh(); authorized=true; }
                            require(!audio::cancelled(),"Cancelled");
                            cloud=std::make_unique<Cloud>(language); cloud->connect(config);
                            std::cout << "SESSION_CONNECTION generation=" << ++connection << std::endl;
                        }
                        cloud->capture_live(front);
                        cloud->play_response();
                        // Wake may arrive just as the last playback buffer drains.
                        {
                            std::lock_guard<std::mutex> guard(commands);
                            if (!audio::session_stopping()) front.mode(rkvoice::VoiceFrontend::Mode::hold);
                        }
                        require(!audio::cancelled(),"Playback cancelled");
                        ++completed;
                        std::cout << "TURN_END index=" << attempted << " status=completed t_ms=" << elapsed_ms() << std::endl;
                    } catch (const std::exception& error) {
                        if (audio::session_stopping()) throw;
                        const bool wake=audio::turn_interrupted.load();
                        const bool timeout=dynamic_cast<const CloudResponseTimeout*>(&error)!=nullptr;
                        const bool disconnected=cloud && !cloud->connected();
                        if (!wake && !timeout && !disconnected) throw;
                        {
                            std::lock_guard<std::mutex> guard(commands);
                            if (audio::session_stopping()) break;
                            front.mode(rkvoice::VoiceFrontend::Mode::hold);
                        }
                        // LivePlayback has already flushed/disconnected here.
                        if (wake) std::cout << "INTERRUPT_BEGIN playback_stopped=true t_ms=" << elapsed_ms() << std::endl;
                        else std::cout << "TURN_RETRY reason=" << (timeout?"response_timeout":"connection_lost")
                                       << " automatic_resend=false t_ms=" << elapsed_ms() << std::endl;
                        const bool retained=cloud && !disconnected && cloud->abort_reply(wake?"wake_word_detected":"response_timeout");
                        if (!retained) {
                            cloud.reset(); authorized=false;
                            if (!audio::session_stopping())
                                std::cout << "CONTEXT_RESET reason=" << (disconnected?"connection_lost":"abort_barrier_unconfirmed") << std::endl;
                        }
                        {
                            std::lock_guard<std::mutex> guard(commands);
                            front.takeWake();
                            if (audio::session_stopping()) break;
                            audio::turn_interrupted=false;
                        }
                        if (wake) ++interrupted;
                        std::cout << "TURN_END index=" << attempted << " status=" << (wake?"interrupted":"retry_needed")
                                  << " t_ms=" << elapsed_ms() << std::endl;
                        std::cout << (wake?"INTERRUPT_DONE":"RECOVERY_DONE") << " context_retained=" << retained << " t_ms=" << elapsed_ms() << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                if (!audio::session_stopping()) {
                    std::cout << "SESSION_ERROR " << e.what() << std::endl;
                    authorized=false;
                }
            }
            {
                std::lock_guard<std::mutex> guard(commands);
                front.takeWake();
                front.mode(rkvoice::VoiceFrontend::Mode::off);
            }
            cloud.reset(); // end the connection only on stop, error, or explicit recovery
            {
                std::lock_guard<std::mutex> guard(commands);
                armed=false; sessionActive=false;
                audio::turn_interrupted=false;
                std::cout << "SESSION_ENDED completed=" << completed << " interrupted=" << interrupted
                          << " cancelled=" << audio::session_stopping() << std::endl;
            }
        }
    } catch (...) {
        quit=true; audio::session_cancelled=true;
        if (control.joinable()) control.join();
        throw;
    }
    quit=true; audio::session_cancelled=true;
    front.mode(rkvoice::VoiceFrontend::Mode::off);
    control.join();
    std::cout << "SERVICE_EXIT microphone=off" << std::endl;
    return 0;
}
#endif
