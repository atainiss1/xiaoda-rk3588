#include "voice_state.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QSurfaceFormat>
#include <QImage>
#include <QMouseEvent>
#include <QDebug>
#include <algorithm>
#include <csignal>

namespace {
volatile std::sig_atomic_t stopped = 0;
void onStop(int) { stopped = 1; }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, onStop);
    std::signal(SIGTERM, onStop);
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(2, 0);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setAlphaBufferSize(0);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    app.setApplicationName("rkvoice-ui");
    const auto args = app.arguments();
    const auto option = [&](const QString& name) {
        const int index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString{};
    };
    QString root = option("--project");
    if (root.isEmpty()) root = QDir(app.applicationDirPath() + "/..").absolutePath();
    root = QDir(root).canonicalPath();
    if (root.isEmpty() || !QFileInfo::exists(root + "/ui/Main.qml")) {
        qCritical() << "UI project directory missing"; return 2;
    }
    QLockFile uiLock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                    (args.contains("--preview")?"/rkvoice-ui-preview.lock":"/rkvoice-ui.lock"));
    uiLock.setStaleLockTime(0);
    if (!uiLock.tryLock(0)) { qCritical() << "Another rkvoice-ui is running"; return 1; }

    VoiceState state;
    QQmlPropertyMap model;
    const auto publish = [&] {
        model.insert("productVisible",state.productVisible);
        model.insert("product",state.product);
        model.insert("secondProduct",state.secondProduct);
        model.insert("productLanguage",state.productLanguage);
        model.insert("productView",state.productView);
        model.insert("productRevision",state.productRevision);
        model.insert("phase", state.phase);
        model.insert("detail", state.detail);
        model.insert("question", state.question);
        model.insert("answer", state.answer);
        model.insert("transcript", state.transcript());
        model.insert("turn", state.turn);
        model.insert("busy", state.busy);
    };
    QFile catalogue(root+"/data/khadas-products.json");
    QVariantMap productData;
    if(catalogue.open(QIODevice::ReadOnly) && catalogue.size()<=65536)
        productData=QJsonDocument::fromJson(catalogue.readAll()).object().value("products").toObject().toVariantMap();
    model.insert("products",productData);
    model.insert("command", "");
    model.insert("ready", false);
    model.insert("wakeEnabled", false);
    model.insert("volume", 50);
    model.insert("notice", "");
    model.insert("preview", args.contains("--preview"));
    publish();

    QProcess voice;
    voice.setWorkingDirectory(root);
    voice.setProcessChannelMode(QProcess::MergedChannels);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("XDG_RUNTIME_DIR", QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    voice.setProcessEnvironment(env);
    QByteArray pending;
    QTimer volumeDebounce;
    volumeDebounce.setSingleShot(true);
    volumeDebounce.setInterval(160);
    QTimer stopDeadline;
    QTimer startupDeadline;
    startupDeadline.setSingleShot(true);
    startupDeadline.setInterval(30000);
    QObject::connect(&startupDeadline, &QTimer::timeout, &app, [&] {
        if (model.value("ready").toBool() || voice.state()==QProcess::NotRunning) return;
        qWarning() << "AUDIO_SETUP_ERROR reason=startup_timeout";
        state.consume("AUDIO_SETUP_ERROR reason=startup_timeout"); publish();
        voice.terminate();
        stopDeadline.start(2000);
    });
    stopDeadline.setSingleShot(true);
    stopDeadline.setInterval(25000); // longer than backend's 20-second operation guard
    QObject::connect(&stopDeadline, &QTimer::timeout, &app, [&] {
        if (voice.state() != QProcess::NotRunning) {
            qWarning() << "UI_FORCE_STOP owned_child=" << voice.processId();
            voice.kill(); // only the QProcess-owned child; never pgrep/kill arbitrary clients
        }
    });
    const auto requestStop = [&] {
        if (!state.busy || state.cancelling) return;
        state.cancel(); publish();
        model.insert("wakeEnabled",false);
        if (voice.state() == QProcess::Running) voice.write("STOP\n");
        stopDeadline.start(25000);
    };
    QObject::connect(&voice, &QProcess::started, &app, [&] {
        model.insert("ready",false);
        state.setupError=false;
        startupDeadline.start();
        qInfo() << "UI_BACKEND_STARTED owned_child=" << voice.processId();
        if (state.cancelling) voice.terminate();
    });
    QObject::connect(&voice, &QProcess::readyReadStandardOutput, &app, [&] {
        pending += voice.readAllStandardOutput();
        if (pending.size() > 128 * 1024) {
            qWarning() << "Backend line exceeded bound";
            pending.clear(); requestStop(); return;
        }
        int end;
        while ((end = pending.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(pending.left(end)).trimmed();
            pending.remove(0, end + 1);
            state.consume(line);
            // Inspect the event itself: READY and SPEECH_START may share one read.
            if (line.startsWith("SERVICE_READY ")) { startupDeadline.stop(); model.insert("ready",true); volumeDebounce.start(); }
            if (line.startsWith("SESSION_ENDED ")) stopDeadline.stop();
            if (line.startsWith("SESSION_STARTED ")) model.insert("notice", "");
            if (line.startsWith("TURN_RETRY "))
                model.insert("notice",QStringLiteral("上一句没有收到回复，请再说一次"));
            if (line.startsWith("CONTEXT_RESET "))
                model.insert("notice",QStringLiteral("连接需要重新建立，之前的约定可能需要再说一次"));
            if (line.startsWith("WAKE_ARMED ")) {
                const bool enabled=line.contains("enabled=1");
                model.insert("wakeEnabled",enabled);
                if (!state.busy) state.detail=enabled?QStringLiteral("说“你好小哒”唤醒，也可以点击开始"):QStringLiteral("麦克风已关闭，点击开始即可说话");
            }
            qInfo().noquote() << line;
        }
        publish();
    });
    QObject::connect(&voice, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &app, [&](int code, QProcess::ExitStatus exitStatus) {
        startupDeadline.stop();
        stopDeadline.stop();
        if (!pending.isEmpty()) {
            state.consume(QString::fromUtf8(pending)); pending.clear();
        }
        state.finish(code, exitStatus == QProcess::CrashExit);
        model.insert("ready",false);
        qInfo() << "UI_BACKEND_EXIT code=" << code << " completed=" << state.completed;
        publish();
    });
    QObject::connect(&voice, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            startupDeadline.stop();
            stopDeadline.stop();
            state.finish(1, true);
            state.detail = QStringLiteral("语音程序未能启动");
            publish();
        }
    });

    // UI volume changes are asynchronous. Device is explicitly the existing ES8388 speaker.
    QProcess volumeProcess;
    volumeProcess.setProcessEnvironment(env);
    const auto applyVolume = [&] {
        if (volumeProcess.state() != QProcess::NotRunning) {
            volumeDebounce.start(); return;
        }
        const int volume = std::clamp(model.value("volume").toInt(), 0, 80);
        volumeProcess.start("pactl", {"set-sink-volume",
            "alsa_output.platform-es8388-sound.HiFi__hw_rockchipes8388__sink",
            QString::number(volume) + "%"});
    };
    QObject::connect(&volumeDebounce, &QTimer::timeout, &app, applyVolume);
    QObject::connect(&volumeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &app, [&](int code, QProcess::ExitStatus) {
        if (code != 0) model.insert("notice", QStringLiteral("音量设置失败，请检查音箱"));
    });
    QObject::connect(&volumeProcess, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError) {
        model.insert("notice", QStringLiteral("暂时无法调整音量"));
    });
    QObject::connect(&model, &QQmlPropertyMap::valueChanged, &app,
                     [&](const QString& key, const QVariant& value) {
        if (key == "volume") {
            if (!args.contains("--preview")) volumeDebounce.start();
        } else if (key == "command") {
            const auto command = value.toString();
            model.insert("command", "");
            if(command=="browse_products" && !state.busy) {
                state.product="overview";state.secondProduct.clear();state.productLanguage="zh";
                state.productView="features";state.productVisible=true;++state.productRevision;publish();return;
            }
            if(command.startsWith("browse_product:") && !state.busy) {
                const auto id=command.mid(15);
                if(VoiceState::productId(id)) {state.product=id;state.productVisible=true;++state.productRevision;publish();}
                return;
            }
            if(command=="hide_product") {state.productVisible=false;publish();return;}
            if (command == "stop") { requestStop(); return; }
            if (command=="wake" && !state.busy && model.value("ready").toBool()) {
                voice.write(model.value("wakeEnabled").toBool()?"WAKE_OFF\n":"WAKE_ON\n"); return;
            }
            if (command != "start" || state.busy || args.contains("--preview")) return;
            if (voice.state()==QProcess::NotRunning) {
                state.phase="preparing"; state.detail=QStringLiteral("正在准备语音，请稍候"); publish();
                voice.start("/bin/bash", {root + "/scripts/run-board.sh", "service"}); return;
            }
            if (!model.value("ready").toBool()) return;
            state.start(); publish();
            model.insert("notice", "");
            voice.write("START\n");
        }
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("voice", &model);
    engine.load(QUrl::fromLocalFile(root + "/ui/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return 1;
    if(args.contains("--preview") && args.contains("--qa-product-controls")) {
        const auto click=[window](int x,int y) {
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(x,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(window,&press);QCoreApplication::sendEvent(window,&release);
        };
        QTimer::singleShot(800,&app,[=]{click(660,163);});
        QTimer::singleShot(2400,&app,[&]{if(!state.productVisible || state.product!="overview"){qCritical()<<"PRODUCT_CONTROLS_FAIL open";app.exit(4);}});
        QTimer::singleShot(2600,&app,[=]{click(160,430);});
        QTimer::singleShot(3300,&app,[&]{if(!state.productVisible || state.product!="mind_pro"){qCritical()<<"PRODUCT_CONTROLS_FAIL select";app.exit(4);}});
        QTimer::singleShot(3500,&app,[=]{click(480,248);});
        QTimer::singleShot(6300,&app,[&]{
            auto* stage=window->findChild<QObject*>("presenterStage");
            if(state.productVisible || !stage || stage->property("productLayout").toDouble()>.001 || stage->property("boardReveal").toDouble()>.001){qCritical()<<"PRODUCT_CONTROLS_FAIL close";app.exit(4);}
        });
        // Retarget while the opening animation is in flight.
        QTimer::singleShot(6500,&app,[=]{click(660,163);});
        QTimer::singleShot(6800,&app,[&]{state.productVisible=false;publish();});
        QTimer::singleShot(7100,&app,[=]{click(660,163);});
        QTimer::singleShot(9000,&app,[&]{
            auto* stage=window->findChild<QObject*>("presenterStage");
            if(!state.productVisible || !stage || stage->property("productLayout").toDouble()<.999 || stage->property("boardReveal").toDouble()<.999){qCritical()<<"PRODUCT_CONTROLS_FAIL retarget";app.exit(4);return;}
            qInfo()<<"PRODUCT_CONTROLS_PASS open select close rapid_retarget microphone=off";
        });
    }
    if (args.contains("--preview") && args.contains("--qa-subtitles")) {
        QTimer::singleShot(1500,&app,[&] {
            state.question=QStringLiteral("请介绍一下你自己，再说说我们接下来可以做什么。");
            state.answer=QStringLiteral("你好，我是小哒。我们可以聊天、了解知识，也可以一起探索有趣的想法。\n这是一段用于检查自动换行的长回复。每次出现新内容，字幕会跟随到底部；手动滑动可以回看前面的内容。\n现在已经超过字幕区域的高度，最后这句话应该完整可见：我在这里，听你说。");
            publish();
        });
        QTimer::singleShot(2500,&app,[&] {
            auto* item=window->findChild<QObject*>("conversationScroll");
            const bool ok=item && item->property("contentY").toDouble()>0 &&
                item->property("contentY").toDouble()+item->property("height").toDouble()>=item->property("contentHeight").toDouble()-1;
            qInfo() << "UI_SUBTITLE_FOLLOW_TEST" << (ok?"PASS":"FAIL");
            if (!ok) app.exit(3);
        });
    }
    if (!args.contains("--preview")) {
        state.phase="preparing"; state.detail=QStringLiteral("首次准备语音，完成后点击即可说话"); publish();
        voice.start("/bin/bash", {root + "/scripts/run-board.sh", "service"});
        if(args.contains("--show-products"))
            QTimer::singleShot(1800,&app,[&]{if(!state.busy){state.productVisible=true;++state.productRevision;publish();}});
    }
    QTimer signalPoll;
    signalPoll.setInterval(100);
    QObject::connect(&signalPoll, &QTimer::timeout, &app, [&] {
        if (stopped) { stopped = 0; window->close(); }
    });
    signalPoll.start();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        if (voice.state() != QProcess::NotRunning) {
            voice.terminate();
            if (!voice.waitForFinished(25000)) { voice.kill(); voice.waitForFinished(2000); }
        }
        if (volumeProcess.state() != QProcess::NotRunning) {
            volumeProcess.terminate(); volumeProcess.waitForFinished(1000);
        }
    });

    // Visual QA options do not start audio or network requests.
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 lastFrame = 0;
    QVector<qint64> intervals;
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
        const auto now = elapsed.nsecsElapsed();
        if (lastFrame > 0 && elapsed.elapsed() > 1500 && intervals.size() < 1000)
            intervals.append(now - lastFrame);
        lastFrame = now;
    }, Qt::QueuedConnection);
    QTimer::singleShot(10000, &app, [&] {
        if (!intervals.isEmpty()) {
            auto sorted = intervals;
            std::sort(sorted.begin(), sorted.end());
            double total = 0;
            for (const auto interval : sorted) total += static_cast<double>(interval);
            qInfo() << "UI_FRAME_SAMPLE count=" << sorted.size()
                    << " mean_fps=" << 1e9 * sorted.size() / total
                    << " p95_ms=" << sorted.at(sorted.size() * 95 / 100) / 1e6;
        }
        const auto path = option("--screenshot");
        if (!path.isEmpty()) qInfo() << "UI_SCREENSHOT saved=" << window->grabWindow().save(path);
    });
    const int quitSeconds = option("--quit-after").toInt();
    if (quitSeconds > 0 && quitSeconds <= 300)
        QTimer::singleShot(quitSeconds * 1000, window, &QWindow::close);
    qInfo() << "UI_READY microphone=off click_to_start=true";
    return app.exec();
}
