#include "voice_state.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value) { if (!value) throw std::runtime_error("UI state regression"); }
int main() {
    VoiceState state;
    state.start();
    state.consume("TURN_BEGIN index=1 total=3");
    state.consume("PREPARING_CAPTURE wait_for_RECORD_READY");
    check(state.phase == "preparing"); // never prompt for speech before actual READY
    state.consume("RECORD_READY mode=vad");
    check(state.phase == "listening");
    state.consume("SPEECH_START captured_ms=500");
    state.consume(QStringLiteral("STT 你好，小哒。"));
    state.consume("LISTEN_STOP t_ms=1000");
    state.consume(QStringLiteral("ASSISTANT 你好！"));
    state.consume("CLOUD_EVENT type=tts state=stop");
    check(state.phase == "thinking"); // received != audibly played
    state.consume("PLAYBACK_START streaming=true");
    check(state.phase == "speaking");
    state.consume("DIALOGUE_DONE seconds=2");
    state.consume("TURN_BEGIN index=2 total=3");
    state.consume("RECORD_READY mode=vad");
    check(state.turn == 2 && state.answer == QStringLiteral("你好！"));
    state.consume("SPEECH_START captured_ms=100");
    check(state.answer.isEmpty() && state.question.isEmpty());
    state.cancel();
    state.consume("PLAYBACK_START streaming=true");
    check(state.phase == "stopping");
    state.finish(130, false);
    check(!state.busy && state.phase == "idle");
    state.start();
    state.consume("NO_SPEECH no_audio_submitted=true");
    state.finish(3, false);
    check(state.phase == "idle" && state.noSpeech == 1);
    state.start();
    state.finish(0, true);
    check(state.phase == "error");
    state.start();
    state.consume(QStringLiteral("ASSISTANT <b>原文</b>"));
    state.consume(QStringLiteral("ASSISTANT <b>原文</b>"));
    check(state.answer.count("<b>") == 2); // no silent deduplication or HTML interpretation
    state.finish(1, false);
    check(state.phase == "error");
    state.start();
    state.consume("SESSION_STARTED continuous=true");
    for (int i=0;i<30;++i) {
        state.consume("RECORD_READY mode=continuous");
        state.consume("SPEECH_START engine=silero");
        state.consume("TURN_BEGIN total=unlimited");
        check(state.phase=="hearing");
        state.consume(QStringLiteral("STT 问题%1").arg(i));
        state.consume(QStringLiteral("ASSISTANT 回复%1").arg(i));
        state.consume("DIALOGUE_DONE seconds=1");
    }
    check(state.busy && state.turn==30 && state.history.size()<=12);
    check(state.transcript().contains(QStringLiteral("回复29")));
    check(!state.transcript().contains(QStringLiteral("问题0")));
    state.cancel();
    state.consume("SESSION_ENDED completed=30 cancelled=1");
    check(!state.busy && state.phase=="idle");
    state.consume("SESSION_STARTED continuous=true");
    state.consume("SESSION_ERROR network failure");
    state.consume("SESSION_ENDED completed=0 cancelled=0");
    check(!state.busy && state.phase=="error");
    state.start(); state.consume("SESSION_STARTED continuous=true");
    state.consume("TURN_BEGIN index=1 connection=new");
    state.consume("PLAYBACK_START streaming=true");
    state.consume("WAKE_DETECTED keyword=test during_reply=1");
    check(state.busy && state.phase=="interrupting" && state.turn==1);
    state.consume("INTERRUPT_BEGIN playback_stopped=true");
    state.consume("INTERRUPT_DONE context_retained=1");
    state.consume("RECORD_READY mode=continuous");
    state.consume("TURN_BEGIN index=2 connection=reused");
    check(state.busy && state.turn==2);
    state.consume("LISTEN_STOP t_ms=1000");
    state.consume("TURN_RETRY reason=response_timeout automatic_resend=false");
    check(state.busy && state.phase=="interrupting");
    state.consume("RECOVERY_DONE context_retained=1");
    state.consume("RECORD_READY mode=continuous");
    check(state.busy && state.phase=="listening");
    state.cancel(); state.consume("WAKE_DETECTED keyword=test during_reply=1");
    state.consume("TURN_RETRY reason=connection_lost");
    state.consume("INTERRUPT_BEGIN playback_stopped=true");
    check(state.phase=="stopping");
    state=VoiceState{};
    state.consume("AUDIO_SETUP_ERROR operation=info code=124");
    state.finish(124,false);
    check(state.phase=="error" && state.setupError && !state.busy);
    check(state.detail.contains(QStringLiteral("本地音频服务响应超时")));
    state.consume("SERVICE_READY engine=silero");
    check(state.phase=="idle" && !state.setupError);
    const QString productEvent=QStringLiteral("PRODUCT_VIEW {\"action\":\"show\",\"product\":\"edge2\",\"second\":\"\",\"language\":\"bilingual\",\"view\":\"interfaces\"}");
    state.consume(productEvent);check(!state.productVisible); // no late event while idle
    state.start();state.consume(productEvent);
    check(state.productVisible && state.product=="edge2" && state.productLanguage=="bilingual" && state.productView=="interfaces");
    const auto rev=state.productRevision;
    state.consume("PRODUCT_VIEW not-json");check(state.productRevision==rev);
    state.consume(QString(productEvent).replace("edge2","../outside"));check(state.productRevision==rev);
    state.cancel();check(!state.productVisible);state.consume(productEvent);check(!state.productVisible);
    state.start();state.consume(productEvent);state.consume("CONTEXT_RESET reason=connection_lost");check(!state.productVisible);
    state.consume(productEvent);state.finish(1,false);check(!state.productVisible);
    for(const auto& id: QStringList{"mind_pro","graphics","graphics2","xplay"}) {
        state.start();state.consume(QString(productEvent).replace("edge2",id));check(state.productVisible && state.product==id);
    }
    std::cout << "UI_STATE_TEST_OK products=expanded_8_late_invalid_stop_reset_guarded\n";
}
