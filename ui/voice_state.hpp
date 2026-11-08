#pragma once
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>

// Parses only the project's documented event prefixes, never arbitrary shell output.
// Text is displayed as PlainText in QML. Duplicate server sentences remain visible.
struct VoiceState {
    QString phase = "idle";
    QString product="overview", secondProduct, productLanguage="zh", productView="features";
    bool productVisible=false;
    int productRevision=0;
    static bool productId(const QString& id) {
        return QStringList{"overview","edge2","vim4","mind2","tea_pro","mind_pro","graphics","graphics2","xplay"}.contains(id);
    }
    QString question;
    QString answer;
    QString detail = QStringLiteral("点击开始，和小哒说说话");
    int turn = 0;
    int completed = 0;
    int noSpeech = 0;
    bool busy = false;
    bool cancelling = false;
    bool setupError = false;
    QStringList history;
    QString transcript() const {
        QStringList all=history;
        if (!question.isEmpty()) all << QStringLiteral("你  ·  ")+question;
        if (!answer.isEmpty()) all << QStringLiteral("小哒  ·  ")+answer;
        return all.join("\n\n");
    }

    void start() {
        *this = VoiceState{};
        busy = true;
        phase = "connecting";
        detail = QStringLiteral("正在连接，请稍候");
    }
    void cancel() {
        productVisible=false;
        cancelling = true;
        phase = "stopping";
        detail = QStringLiteral("正在结束本次聊天");
    }
    void consume(const QString& line) {
        if(line.startsWith("PRODUCT_VIEW ")) {
            if(cancelling || !busy || line.size()>2048) return;
            const auto obj=QJsonDocument::fromJson(line.mid(13).toUtf8()).object();
            const auto action=obj.value("action").toString(),id=obj.value("product").toString();
            const auto other=obj.value("second").toString(),lang=obj.value("language").toString(),view=obj.value("view").toString();
            if(!productId(id) || !QStringList{"zh","en","bilingual"}.contains(lang) || !QStringList{"features","interfaces"}.contains(view))return;
            if(action=="hide") {productVisible=false;++productRevision;return;}
            if(action!="show" && action!="compare")return;
            if(action=="compare" && (!productId(other) || other=="overview" || other==id || id=="overview"))return;
            product=id;secondProduct=action=="compare"?other:QString{};
            productLanguage=lang;productView=view;productVisible=true;++productRevision;return;
        }
        if(line.startsWith("SESSION_ENDED ") || line.startsWith("SESSION_ERROR ") || line.startsWith("AUDIO_SETUP_ERROR ") || line.startsWith("CONTEXT_RESET "))productVisible=false;
        if (line.startsWith("AUDIO_SETUP_ERROR ")) {
            setupError=true; busy=false; phase="error";
            detail=QStringLiteral("本地音频初始化失败，请点击重试语音");
            if (line.contains("code=124") || line.contains("reason=startup_timeout"))
                detail=QStringLiteral("本地音频服务响应超时，请点击重试语音");
            return;
        }
        if (line.startsWith("SERVICE_READY ")) {
            setupError=false;
            phase="idle"; detail=QStringLiteral("点击开始即可说话，点击停止结束"); return;
        }
        if (line.startsWith("SESSION_STARTED ")) {
            if (cancelling) return;
            busy=true; cancelling=false; phase="listening"; turn=0; completed=0;
            detail=QStringLiteral("现在可以说话，会一直聊到你点停止"); return;
        }
        if (line.startsWith("SESSION_ERROR ")) {
            phase="error"; detail=QStringLiteral("语音处理遇到问题，请重新开始"); return;
        }
        if (line.startsWith("SESSION_ENDED ")) {
            busy=false;
            if (phase!="error") { phase="idle"; detail=QStringLiteral("已停止，麦克风已关闭"); }
            cancelling=false; return;
        }
        if (line.startsWith("STT ")) question = line.mid(4).left(4096);
        if (line.startsWith("ASSISTANT ")) {
            const auto sentence = line.mid(10).left(4096);
            if(sentence.startsWith("% self.khadas.present"))return;
            answer = (answer + (answer.isEmpty() ? "" : "\n") + sentence).right(12000);
        }
        // Late data must not replace the visible cancellation state.
        if (cancelling) return;
        if (line.startsWith("WAKE_DETECTED ") && line.contains("during_reply=1")) {
            phase="interrupting"; detail=QStringLiteral("听到你叫我，正在停下回答"); return;
        }
        if (line.startsWith("INTERRUPT_BEGIN ")) {
            phase="interrupting"; detail=QStringLiteral("已停止播放，正在准备听你说"); return;
        }
        if (line.startsWith("TURN_RETRY ")) {
            phase="interrupting"; detail=QStringLiteral("这句没有收到回复，正在恢复收音"); return;
        }
        if (line.startsWith("TURN_BEGIN ")) {
            ++turn;
            if (phase!="hearing") { phase = "connecting"; detail = QStringLiteral("正在准备第 %1 轮").arg(turn); }
        } else if (line.startsWith("PREPARING_CAPTURE ")) {
            phase = "preparing";
            detail = QStringLiteral("麦克风准备中，请稍候");
        } else if (line.startsWith("RECORD_READY ")) {
            phase = "listening";
            detail = QStringLiteral("现在可以说话，说完停顿即可");
        } else if (line.startsWith("SPEECH_START ")) {
            phase = "hearing";
            if (!question.isEmpty()) history << QStringLiteral("你  ·  ")+question;
            if (!answer.isEmpty()) history << QStringLiteral("小哒  ·  ")+answer;
            while (history.size()>12 || history.join("\n").size()>16000) history.removeFirst();
            question.clear();
            answer.clear();
            detail = QStringLiteral("我在听，说完自然停顿");
        } else if (line.startsWith("LISTEN_STOP ")) {
            phase = "thinking";
            detail = QStringLiteral("正在想怎么回答你");
        } else if (line.startsWith("PLAYBACK_START ")) {
            phase = "speaking";
            detail = QStringLiteral("说“你好小哒”可以打断，停下后再提问");
        } else if (line.startsWith("DIALOGUE_DONE ")) {
            ++completed;
            phase = "between";
            detail = QStringLiteral("这一轮聊完啦");
        } else if (line.startsWith("NO_SPEECH ")) {
            ++noSpeech;
            phase = "between";
            detail = QStringLiteral("这轮没有听到你说话");
        }
    }
    void finish(int code, bool crashed) {
        productVisible=false;
        busy = false;
        if (cancelling || (!crashed && code == 130)) {
            phase = "idle";
            detail = QStringLiteral("已停止，想聊时再点开始");
        } else if (setupError) {
            phase = "error"; // Preserve the actionable local setup error.
        } else if (!crashed && code == 0 && completed > 0) {
            phase = "idle";
            detail = QStringLiteral("聊完啦，随时可以再开始");
        } else if (!crashed && code == 3) {
            phase = "idle";
            detail = QStringLiteral("没有听到说话，点击后可以再试");
        } else if (!crashed && code == 124) {
            phase = "error";
            detail = QStringLiteral("音频设备响应超时，请重新开始");
        } else {
            phase = "error";
            detail = QStringLiteral("语音服务已停止，请重新开始");
        }
    }
};
