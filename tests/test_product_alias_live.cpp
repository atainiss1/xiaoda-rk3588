#define RKVOICE_CLOUD_TEST
#include "../src/cloud_main.cpp"
namespace {
struct CloudTest {
 static void run(int argc,char** argv) {
  auto cfg=Config::load();cfg.refresh();

  for(int i=1;i<argc;++i){
   std::cout<<"TEST_SYNTHETIC_SPEECH index="<<i<<" file="<<argv[i]<<std::endl;
   Cloud c;c.connect(cfg);std::this_thread::sleep_for(2500ms);
   const auto wave=audio::load_wave(argv[i]);
   require(wave.sample_rate==16000 && wave.channels==1,"Test WAV must be mono 16kHz");
   c.begin_turn();auto encoder=make_encoder();
   c.send({{"session_id",c.session_},{"type","listen"},{"state","start"},{"mode","manual"}});
   std::array<opus_int16,960> samples{};std::array<unsigned char,4000> packet{};
   for(std::size_t offset=0;offset<wave.pcm.size();offset+=1920){
    samples.fill(0);const auto count=std::min(std::size_t{1920},wave.pcm.size()-offset);
    for(std::size_t n=0;n<count/2;++n)samples[n]=static_cast<opus_int16>(rkvoice::decode16(wave.pcm.data()+offset+n*2));
    const int size=opus_encode(encoder.get(),samples.data(),960,packet.data(),4000);
    require(size>0,"Test encoding failed");c.send_audio(packet.data(),size);std::this_thread::sleep_for(40ms);
   }
   c.send({{"session_id",c.session_},{"type","listen"},{"state","stop"}});
   std::vector<unsigned char> pcm;
   while(true){
    std::unique_lock<std::mutex> lock(c.mutex_);
    c.check_response_timeout(std::chrono::steady_clock::now());
    require(c.failure_.empty(),"Live test cloud failure");
    if(!c.reply_.empty()) {auto bytes=std::move(c.reply_.front());c.reply_.pop_front();pcm.insert(pcm.end(),bytes.begin(),bytes.end());}
    else if(c.done_)break;
    else c.changed_.wait_for(lock,100ms);
   }
   require(!pcm.empty(),"No speech returned");
   // Capture only synthesized cloud output. No microphone or speaker is opened.
   const char* out=std::getenv("RKVOICE_TEST_OUTPUT");
   if(out){std::ofstream f(std::string(out)+"/reply"+std::to_string(i)+".pcm",std::ios::binary);f.write(reinterpret_cast<const char*>(pcm.data()),static_cast<std::streamsize>(pcm.size()));}
   std::cout<<"TEST_REPLY index="<<i<<" rate="<<c.rate_<<" bytes="<<pcm.size()<<" microphone=off speaker=off"<<std::endl;
  }
 }
};
}
int main(int argc,char**argv){try{CloudTest::run(argc,argv);return 0;}catch(const std::exception&e){std::cerr<<"TEST_FAILED "<<e.what()<<std::endl;return 1;}}
