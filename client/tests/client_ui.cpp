// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "kvmwindow.h"
#include "rkmoon_audio_keys.h"
#include "rkmoon_audio_control.h"
#include "rkmoon_auth.h"
#include "rkmoon_pointer.h"
#include "rkmoon_session_control.h"
#include "rkmoon_http_log.h"
#include "settings/streamingpreferences.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

#include <exception>
#include <functional>
#include <thread>

namespace {
QStringList captured;
void capture(QtMsgType, const QMessageLogContext&, const QString& text) { captured.append(text); }

// The exact wire value the server contract requires: base64(UTF-8("kvm:" + password)).
const QByteArray DEFAULT_AUTHORIZATION = QByteArrayLiteral("Basic a3ZtOmt2bQ==");
const QUrl TARGET_URL = QUrl(QStringLiteral("http://192.0.2.7:47989/serverinfo"));

// ---------------------------------------------------------------------------
// Loopback server used by the optional integration slots below. It answers on
// 127.0.0.1 only, inside this process, and never reaches a device or a network host.
class LoopbackServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const QByteArray& path, const QByteArray& authorization)>;

    Handler handler;

    int requests = 0;
    QList<QByteArray> seenAuthorization;
    QList<QByteArray> seenPaths;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);

        auto* buffer = new QByteArray;
        QObject::connect(socket, &QObject::destroyed, socket, [buffer] { delete buffer; });
        QObject::connect(socket, &QIODevice::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            QByteArray path;
            QByteArray authorization;
            for (int i = 0; i < lines.size(); i++) {
                const QByteArray line = lines[i].trimmed();
                if (i == 0) {
                    const QList<QByteArray> parts = line.split(' ');
                    if (parts.size() > 1) {
                        path = parts[1];
                    }
                }
                else if (line.toLower().startsWith("authorization:")) {
                    authorization = line.mid(line.indexOf(':') + 1).trimmed();
                }
            }
            requests++;
            seenAuthorization.append(authorization);
            seenPaths.append(path.left(path.indexOf('?')));
            const QByteArray body = handler ? handler(path, authorization) : QByteArray();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->flush();
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
    }
};

QByteArray serverInfoXml(bool authorized)
{
    if (!authorized) {
        // Exactly how the server reports a missing or wrong password: HTTP 200 carrying a
        // GameStream document whose root status_code is 401.
        return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                          "<root status_code=\"401\" status_message=\"Unauthorized\"></root>");
    }
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root status_code=\"200\">"
                      "<hostname>rkmoon-loopback</hostname>"
                      "<appversion>7.1.431.0</appversion>"
                      "<GfeVersion>3.23.0.74</GfeVersion>"
                      "<uniqueid>SYNTHETIC-LOOPBACK-UUID</uniqueid>"
                      "<mac>00:00:00:00:00:00</mac>"
                      "<LocalIP>127.0.0.1</LocalIP>"
                      "<ServerCodecModeSupport>259</ServerCodecModeSupport>"
                      "<currentgame>0</currentgame>"
                      "<state>SUNSHINE_SERVER_FREE</state>"
                      "<HttpsPort>0</HttpsPort>"
                      "<PairStatus>1</PairStatus>"
                      "<RKMoonAuth>password-http-v1</RKMoonAuth><RKMoonDisplayVersion>1</RKMoonDisplayVersion><RKMoonDisplayStatus>ready</RKMoonDisplayStatus><RKMoonDisplayWidth>2560</RKMoonDisplayWidth><RKMoonDisplayHeight>1440</RKMoonDisplayHeight><RKMoonDisplayFpsX100>8999</RKMoonDisplayFpsX100>"
                      "</root>");
}

QByteArray appListXml()
{
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root status_code=\"200\">"
                      "<App><AppTitle>HDMI</AppTitle><ID>881448767</ID></App></root>");
}

// KvmHost blocks on nested event loops, so it runs on a worker thread while the loopback
// servers keep the main thread's event loop free to accept and answer connections.
QString runOffMainThread(std::function<void()> work)
{
    QEventLoop loop;
    QString failure;
    std::thread worker([&] {
        try { work(); }
        catch (const std::exception& e) { failure = QString::fromUtf8(e.what()); }
        catch (...) { failure = QStringLiteral("unknown exception"); }
        QMetaObject::invokeMethod(&loop, [&] { loop.quit(); }, Qt::QueuedConnection);
    });
    loop.exec();
    worker.join();
    return failure;
}
}

class ClientUiTest : public QObject
{
    Q_OBJECT
private slots:
    void redactedHttpLogs()
    {
        captured.clear();
        const auto previous = qInstallMessageHandler(capture);
        // Synthetic sentinels only. No real key, certificate, password or network operation.
        rkmoonLogHttpRequest("launch");
        rkmoonLogHttpRequest("https://invalid/launch?rikey=SYNTHETIC_RIKEY&clientcert=SYNTHETIC_CERT");
        rkmoonLogHttpRequest("serverinfo?password=SYNTHETIC_PASSWORD", true);
        rkmoonLogHttpRequest("resume", true);
        qInstallMessageHandler(previous);
        QCOMPARE(captured.size(), 4);
        const auto joined = captured.join('\n');
        QVERIFY(joined.contains("launch"));
        QVERIFY(joined.contains("resume"));
        QVERIFY(!joined.contains("SYNTHETIC"));
        QVERIFY(!joined.contains("rikey"));
        QVERIFY(!joined.contains("clientcert"));
        QVERIFY(!joined.contains("password"));
        QVERIFY(!joined.contains("https://"));
    }
    void modifiersAndKeyRelease()
    {
        RkmoonAudioKeys keys;
        SDL_Event ev = {};
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_F9;
        ev.key.keysym.mod = KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT;
        RkmoonAudioControl::setMuted(false);
        QCOMPARE(keys.filter(ev), 0);
        QVERIFY(RkmoonAudioControl::isMuted());
        ev.key.repeat = 1;
        ev.key.keysym.mod = 0;
        QCOMPARE(keys.filter(ev), 0);
        QVERIFY(RkmoonAudioControl::isMuted());
        ev.type = SDL_KEYUP;
        QCOMPARE(keys.filter(ev), 0); // Consume release after modifier release.
        ev.type = SDL_KEYDOWN; ev.key.repeat = 0;
        ev.key.keysym.mod = KMOD_RCTRL | KMOD_RALT | KMOD_RSHIFT;
        QCOMPARE(keys.filter(ev), 0);
        QVERIFY(!RkmoonAudioControl::isMuted());
        keys.reset();
        ev.key.keysym.mod = KMOD_CTRL | KMOD_ALT; // No shift: remote F9 untouched.
        QCOMPARE(keys.filter(ev), 1);
        ev.key.keysym.mod = KMOD_LCTRL | KMOD_RALT | KMOD_LSHIFT;
        ev.key.keysym.sym = SDLK_F10;
        RkmoonAudioControl::setVolumePercent(100);
        QCOMPARE(keys.filter(ev), 0);
        QCOMPARE(RkmoonAudioControl::volumePercent(), 90);
        ev.type = SDL_KEYUP; QCOMPARE(keys.filter(ev), 0);
        ev.type = SDL_KEYDOWN; ev.key.keysym.sym = SDLK_F11;
        QCOMPARE(keys.filter(ev), 0);
        QCOMPARE(RkmoonAudioControl::volumePercent(), 100);
    }
    void fixedRelativeAndHardwarePolicy()
    {
        KvmConfig config;
        auto* preferences = StreamingPreferences::get();
        config.applyTo(*preferences);
        QVERIFY(preferences->quitAppAfter);
        QVERIFY(preferences->absoluteMouseMode);
        QVERIFY(preferences->absoluteTouchMode);
        QVERIFY(!preferences->multiController);
        QVERIFY(!preferences->enableMdns);
        QCOMPARE(preferences->audioConfig, StreamingPreferences::AC_STEREO);
        QCOMPARE(preferences->videoDecoderSelection, StreamingPreferences::VDS_FORCE_HARDWARE);
    }
    void experimental1440p90RequestsExactParameters()
    {
        KvmConfig config;
        QCOMPARE(config.width, 1920);
        QCOMPARE(config.height, 1080);
        QCOMPARE(config.fps, 60);
        config.width = 2560;
        config.height = 1440;
        config.fps = 90;
        config.save();
        KvmConfig reloaded;
        QCOMPARE(reloaded.width, 1920);
        QCOMPARE(reloaded.height, 1080);
        QCOMPARE(reloaded.fps, 60);
        auto* preferences = StreamingPreferences::get();
        config.applyTo(*preferences);
        QCOMPARE(preferences->width, 2560);
        QCOMPARE(preferences->height, 1440);
        QCOMPARE(preferences->fps, 90);
        config.width = 1920;
        config.height = 1080;
        config.fps = 60;
        config.save();
    }
    void onlyBitrateAndCodecSettings()
    {
        KvmWindow window;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->findChildren<QSpinBox*>().size(), 1);
            QCOMPARE(dialog->findChildren<QComboBox*>().size(), 1);
            auto* bitrate = dialog->findChild<QSpinBox*>();
            QCOMPARE(bitrate->maximum(), 35000);
            bitrate->setValue(35000);
            dialog->accept();
        });
        window.settings();
        QCOMPARE(window.m_config.bitrateKbps, 35000);
    }
    void mouseModesPersistAndChooseTouchPolicy()
    {
        KvmConfig config;
        config.mouseMode = KvmConfig::MOUSE_RELATIVE; config.save();
        KvmConfig relative;
        QCOMPARE(relative.mouseMode, KvmConfig::MOUSE_RELATIVE);
        relative.applyTo(*StreamingPreferences::get());
        QVERIFY(!StreamingPreferences::get()->absoluteMouseMode);
        QVERIFY(!StreamingPreferences::get()->absoluteTouchMode);
        KvmWindow window;
        auto* mode = window.findChild<QComboBox*>("mouseMode");
        QVERIFY(mode);
        QCOMPARE(mode->parent(), &window);
        mode->setCurrentIndex(0);
        for (auto* busy : {&window.m_streaming, &window.m_connecting, &window.m_pollBusy, &window.m_cleanup}) {
            *busy = true; window.updateMouseModeEnabled();
            QVERIFY(!mode->isEnabled());
            mode->setCurrentIndex(1);
            QCOMPARE(mode->currentIndex(), 0);
            QCOMPARE(window.m_config.mouseMode, KvmConfig::MOUSE_ABSOLUTE);
            *busy = false; window.updateMouseModeEnabled();
            QVERIFY(mode->isEnabled());
        }
        KvmConfig absolute;
        QCOMPARE(absolute.mouseMode, KvmConfig::MOUSE_ABSOLUTE);
        absolute.applyTo(*StreamingPreferences::get());
        QVERIFY(StreamingPreferences::get()->absoluteMouseMode);
        QVERIFY(StreamingPreferences::get()->absoluteTouchMode);
    }
    void launchAndResumeDeclareMouseMode()
    {
        RkmoonAuth::setPassword("kvm");
        RkmoonAuth::bindTarget("192.0.2.7",47989);
        for (const auto command : {QString("launch"),QString("resume")}) {
            for (bool absolute : {false,true}) {
                RkmoonAuth::setMouseMode(absolute);
                QUrl url("http://192.0.2.7:47989/"+command+"?rkmoonMouseMode=stale&appid=1");
                RkmoonAuth::prepareSessionUrl(url,command);
                QUrlQuery query(url);
                QCOMPARE(query.allQueryItemValues("rkmoonMouseMode").size(),1);
                QCOMPARE(query.queryItemValue("rkmoonMouseMode"),absolute?QString("absolute"):QString("relative"));
                QCOMPARE(query.queryItemValue("appid"),QString("1"));
            }
        }
        QUrl other("http://192.0.2.8:47989/launch");
        RkmoonAuth::prepareSessionUrl(other,"launch");
        QVERIFY(other.query().isEmpty());
        QUrl info("http://192.0.2.7:47989/serverinfo");
        RkmoonAuth::prepareSessionUrl(info,"serverinfo");
        QVERIFY(info.query().isEmpty());
    }
    void videoPointerMapping()
    {
        // 16:9 in a 4:3 viewport: letterbox. Half-open edges reject black bars.
        auto r = RkmoonPointer::videoRect(2560,1440,1200,900);
        QCOMPARE(r.x,0); QCOMPARE(r.y,112); QCOMPARE(r.w,1200); QCOMPARE(r.h,675);
        QVERIFY(!RkmoonPointer::inside(400,111,r));
        QVERIFY(!RkmoonPointer::inside(400,787,r));
        auto p = RkmoonPointer::position(1199,786,r);
        QCOMPARE(p.x,short(1199)); QCOMPARE(p.y,short(674));
        p = RkmoonPointer::position(-100,1000,r);
        QCOMPARE(p.x,short(0)); QCOMPARE(p.y,short(674));
        // 4:3 on ultrawide: pillarbox, dynamically recomputed for source change.
        auto portrait = RkmoonPointer::videoRect(1600,1200,2400,1080);
        QCOMPARE(portrait.x,480); QCOMPARE(portrait.w,1440);
        QVERIFY(!RkmoonPointer::inside(479,400,portrait));
        QVERIFY(RkmoonPointer::inside(480,400,portrait));
        // HiDPI: event and SDL window coordinates remain in the same units.
        // Equivalent physical viewport gives the same normalized interior point.
        auto logical = RkmoonPointer::videoRect(1920,1080,1280,800);
        auto physical = RkmoonPointer::videoRect(1920,1080,2560,1600);
        auto l = RkmoonPointer::position(640,400,logical);
        auto h = RkmoonPointer::position(1280,800,physical);
        QVERIFY(qAbs(double(l.x)/(logical.w-1)-double(h.x)/(physical.w-1)) < .001);
        QVERIFY(qAbs(double(l.y)/(logical.h-1)-double(h.y)/(physical.h-1)) < .002);
        QVERIFY(!RkmoonPointer::inside(0,0,RkmoonPointer::videoRect(0,0,0,0)));
    }
    void displayContractAndJitter()
    {
        auto xml = [](int fps) { return QString("<root><RKMoonDisplayVersion>1</RKMoonDisplayVersion><RKMoonDisplayStatus>ready</RKMoonDisplayStatus><RKMoonDisplayWidth>2560</RKMoonDisplayWidth><RKMoonDisplayHeight>1440</RKMoonDisplayHeight><RKMoonDisplayFpsX100>%1</RKMoonDisplayFpsX100></root>").arg(fps); };
        const auto mode = RkmoonDisplay::parse(xml(8999));
        QVERIFY(mode.ready());
        QCOMPARE((mode.fpsX100 + 50) / 100, 90);
        QVERIFY(mode == RkmoonDisplay::parse(xml(9000)));
        QVERIFY(mode != RkmoonDisplay::parse(xml(6000)));
        QVERIFY_EXCEPTION_THROWN(RkmoonDisplay::parse("<root/>"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(RkmoonDisplay::parse(xml(0)), std::runtime_error);
        auto waiting = RkmoonDisplay::parse("<root><RKMoonDisplayVersion>1</RKMoonDisplayVersion><RKMoonDisplayStatus>no_signal</RKMoonDisplayStatus></root>");
        QVERIFY(!waiting.ready());
        QCOMPARE(waiting.width, 0);
    }
    void asynchronousDisplayPoll()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.handler = [](const QByteArray&, const QByteArray&) { return serverInfoXml(true); };
        KvmConfig config;
        config.address = "127.0.0.1"; config.httpPort = server.serverPort(); config.hostUuid.clear();
        KvmHost host(config);
        host.setPassword("kvm"); RkmoonAuth::bindTarget(config.address, config.httpPort);
        bool completed = false;
        QString error;
        host.refreshAsync([&](QString result) { completed = true; error = result; });
        QVERIFY(!completed);
        QTRY_VERIFY(completed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(host.display.width, 2560);
        QCOMPARE(host.display.fpsX100, 8999);
    }
    void mouseCapabilitiesAreExplicitWithRelativeLegacySupport()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray modes;
        server.handler = [&](const QByteArray&, const QByteArray&) {
            auto xml = serverInfoXml(true);
            if (!modes.isEmpty()) xml.replace("</root>", "<RKMoonMouseModes>"+modes+"</RKMoonMouseModes></root>");
            return xml;
        };
        KvmConfig config;
        config.address = "127.0.0.1"; config.httpPort = server.serverPort(); config.hostUuid.clear();
        KvmHost host(config);
        host.setPassword("kvm"); RkmoonAuth::bindTarget(config.address,config.httpPort);
        for (const auto advertised : {QByteArray(),QByteArray("relative"),QByteArray("relative,absolute"),QByteArray("invalid")}) {
            modes = advertised;
            bool done = false; QString error;
            host.refreshAsync([&](QString e) { error = e; done = true; });
            QTRY_VERIFY(done);
            QVERIFY2(error.isEmpty(),qPrintable(error));
            QCOMPARE(host.supportsAbsoluteMouse, advertised == "relative,absolute");
            QCOMPARE(host.supportsRelativeMouse, advertised != "invalid");
        }
    }
    void arbitrarySourceModesStayUncapped()
    {
        for (const auto mode : {QSize(1920,1280), QSize(2560,1600), QSize(3000,2000), QSize(3840,2160)}) {
            for (int rate : {100, 5994, 7550, 9000, 12000}) {
                auto xml = serverInfoXml(true);
                xml.replace("2560", QByteArray::number(mode.width()));
                xml.replace("1440", QByteArray::number(mode.height()));
                xml.replace("8999", QByteArray::number(rate));
                auto parsed = RkmoonDisplay::parse(xml);
                KvmConfig config; config.width = parsed.width; config.height = parsed.height;
                config.fps = (parsed.fpsX100 + 50) / 100;
                auto& prefs = *StreamingPreferences::get(); config.applyTo(prefs);
                QCOMPARE(prefs.width, mode.width()); QCOMPARE(prefs.height, mode.height());
                QCOMPARE(prefs.fps, (rate + 50) / 100);
            }
        }
        for (auto value : {QByteArray("3842"), QByteArray("1919")}) {
            auto xml = serverInfoXml(true); xml.replace("2560", value);
            QVERIFY_EXCEPTION_THROWN(RkmoonDisplay::parse(xml), std::runtime_error);
        }
    }
    void pollTransportBudgetAndAuthFailClosed()
    {
        LoopbackServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        auto port = server.serverPort(); server.close();
        KvmWindow window; window.m_config.address = "127.0.0.1";
        window.m_config.httpPort = port; window.m_config.hostUuid.clear();
        window.m_host.setPassword("kvm"); RkmoonAuth::bindTarget("127.0.0.1", port);
        window.m_streaming = true; window.m_wantStream = true;
        RkmoonSessionControl::initialize();
        for (int n = 1; n <= 10; ++n) {
            window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
            QCOMPARE(window.m_pollFailures, n);
            QCOMPARE(RkmoonSessionControl::internalQuitPending.load(), n == 10);
            QCOMPARE(window.m_wantStream, n < 10);
        }
        QVERIFY(server.listen(QHostAddress::LocalHost, port));
        server.handler = [](const QByteArray&, const QByteArray&) { return serverInfoXml(false); };
        RkmoonSessionControl::initialize(); window.m_pollFailures = 0; window.m_wantStream = true;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_host.transientFailure);
        QVERIFY(RkmoonSessionControl::internalQuitPending.load());
        QVERIFY(!window.m_wantStream);
        window.m_streaming = false; window.stopFollowing(); RkmoonSessionControl::initialize();
    }
    void unexpectedEndReconnectsWithBackoff()
    {
        KvmWindow window;
        RkmoonSessionControl::initialize();
        window.m_start->setEnabled(true);
        window.m_wantStream = true;
        window.m_sessionConnected = true; // Playback had connected, then ended unexpectedly.
        const int delays[] = {1000, 2000, 4000, 8000, 8000};
        for (int n = 1; n <= 5; ++n) {
            window.sessionEnded();
            QVERIFY(window.m_wantStream);
            QCOMPARE(window.m_reconnectAttempt, n);
            QVERIFY(window.m_reconnectPending && window.m_reconnectTimer.isActive());
            QCOMPARE(window.m_reconnectTimer.interval(), delays[n - 1]);
            QVERIFY(window.m_status->text().startsWith(QString::fromUtf16(u"\u8fde\u63a5\u4e2d\u65ad\uff0c\u6b63\u5728\u91cd\u8fde\uff08%1/5\uff09").arg(n)));
            window.m_reconnectTimer.stop(); window.m_reconnectPending = false;
            window.m_sessionConnected = false; // The relaunch failed before connecting.
        }
        window.sessionEnded();
        QVERIFY(!window.m_wantStream && !window.m_reconnectTimer.isActive());
        QCOMPARE(window.m_reconnectAttempt, 0);

        // connectionStarted clears the counter; the next drop starts again at 1 s.
        window.m_wantStream = true; window.m_sessionConnected = true;
        window.sessionEnded(); window.sessionEnded();
        QCOMPARE(window.m_reconnectAttempt, 2);
        window.markConnected();
        QCOMPARE(window.m_reconnectAttempt, 0);
        QVERIFY(!window.m_reconnectTimer.isActive());
        window.sessionEnded();
        QCOMPARE(window.m_reconnectTimer.interval(), 1000);

        // The main button cancels a pending reconnect.
        QVERIFY(window.m_start->isEnabled());
        window.startStream();
        QVERIFY(!window.m_wantStream && !window.m_reconnectTimer.isActive());
        QCOMPARE(window.m_reconnectAttempt, 0);

        // User exit and a first launch that never connected do not auto-reconnect.
        window.m_wantStream = true; window.m_sessionConnected = true;
        RkmoonSessionControl::userQuit.store(true);
        window.sessionEnded();
        QVERIFY(!window.m_wantStream && !window.m_reconnectTimer.isActive());
        RkmoonSessionControl::initialize();
        window.m_wantStream = true; window.m_sessionConnected = false;
        window.sessionEnded();
        QVERIFY(!window.m_wantStream && !window.m_reconnectTimer.isActive());
    }
    void quitReasonsAreThreadSafe()
    {
        RkmoonSessionControl::initialize();
        SDL_Event event{}; event.type = SDL_USEREVENT;
        RkmoonSessionControl::observe(event);
        QVERIFY(!RkmoonSessionControl::userQuit.load());
        RkmoonSessionControl::stop();
        QVERIFY(RkmoonSessionControl::internalQuitPending.exchange(false));
        QVERIFY(!RkmoonSessionControl::userQuit.load());
        std::thread producer([] {
            SDL_Event quit{}; quit.type = SDL_QUIT;
            RkmoonSessionControl::observe(quit);
        });
        producer.join();
        QVERIFY(RkmoonSessionControl::userQuit.load());
        RkmoonSessionControl::initialize();
        QVERIFY(!RkmoonSessionControl::userQuit.load());
    }
    void upstreamEventsNeverStopSession()
    {
        QCOMPARE(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
        // Cold allocator: preceding tests never register SDL events. Run this slot
        // in a fresh process (also run standalone during release verification).
        // Reproduce the old bug using the allocator, not an invented event id.
        const auto allocated = SDL_RegisterEvents(1);
        QCOMPARE(allocated, Uint32(SDL_USEREVENT));
        for (int session = 0; session < 2; ++session) {
            RkmoonSessionControl::initialize();
            SDL_Event barrier{}; barrier.type = SDL_USEREVENT; barrier.user.code = 100; // pinned session.cpp barrier
            SDL_Event frame{}; frame.type = SDL_USEREVENT; frame.user.code = 0; // pinned decoder.h frame-ready
            QCOMPARE(SDL_PushEvent(&barrier), 1);
            QCOMPARE(SDL_PushEvent(&frame), 1);
            SDL_Event received{};
            int count = 0;
            while (SDL_PollEvent(&received)) {
                if (received.type != SDL_USEREVENT) continue;
                ++count;
                // The previous type-only exit condition matches both ordinary events.
                QCOMPARE(received.type, allocated);
                RkmoonSessionControl::observe(received);
                QVERIFY(!RkmoonSessionControl::userQuit.load());
                QVERIFY(!RkmoonSessionControl::internalQuitPending.exchange(false));
            }
            QCOMPARE(count, 2);
            std::thread terminate([] { RkmoonSessionControl::stop(); });
            terminate.join();
            QVERIFY(RkmoonSessionControl::internalQuitPending.exchange(false));
            QVERIFY(!RkmoonSessionControl::internalQuitPending.exchange(false));
            QVERIFY(!RkmoonSessionControl::userQuit.load());
        }
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
    }
    void unsupportedSourcePollWaitsAndCloseIsGuarded()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        server.handler = [](const QByteArray&, const QByteArray&) {
            auto body = serverInfoXml(true);
            // no_signal now starts a placeholder session (ADR-011); an unsupported mode still waits.
            body.replace("<RKMoonDisplayStatus>ready", "<RKMoonDisplayStatus>unsupported");
            return body;
        };
        KvmWindow window;
        window.m_config.address = "127.0.0.1";
        window.m_config.httpPort = server.serverPort();
        window.m_config.hostUuid.clear();
        window.m_host.setPassword("kvm");
        RkmoonAuth::bindTarget(window.m_config.address, window.m_config.httpPort);
        window.m_wantStream = true;
        window.pollDisplay();
        QVERIFY(window.m_pollBusy);
        QCloseEvent close;
        QApplication::sendEvent(&window, &close);
        QVERIFY(!close.isAccepted());
        QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(window.m_wantStream);
        QVERIFY(!window.m_streaming);
        QVERIFY(!window.m_host.display.ready());
        window.stopFollowing();
        QVERIFY(!window.m_wantStream);
        QVERIFY(!window.m_poll.isActive());
    }
    void displayChangesRespectSessionLifecycle()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int fps = 9000;
        server.handler = [&](const QByteArray&, const QByteArray&) {
            auto body = serverInfoXml(true);
            body.replace("<RKMoonDisplayFpsX100>8999", QByteArray("<RKMoonDisplayFpsX100>") + QByteArray::number(fps));
            return body;
        };
        KvmWindow window;
        window.m_config.address = "127.0.0.1";
        window.m_config.httpPort = server.serverPort(); window.m_config.hostUuid.clear();
        window.m_host.setPassword("kvm");
        RkmoonAuth::bindTarget(window.m_config.address, window.m_config.httpPort);
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        window.m_runningMode = window.m_host.display;
        window.m_streaming = true; window.m_wantStream = true;
        RkmoonSessionControl::initialize();
        fps = 8999;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_restart);
        QVERIFY(!RkmoonSessionControl::internalQuitPending.load());
        fps = 6000;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_restart); // First sighting of a new mode only arms the debounce.
        QVERIFY(!RkmoonSessionControl::internalQuitPending.load());
        fps = 5000;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_restart); // A different mode during switching restarts the count.
        fps = 6000;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_restart);
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(window.m_restart);
        QVERIFY(RkmoonSessionControl::internalQuitPending.exchange(false));
        window.m_cleanup = true; window.m_restart = false;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!RkmoonSessionControl::internalQuitPending.load());
        window.m_cleanup = false;
        RkmoonSessionControl::userQuit.store(true);
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_wantStream);
        QVERIFY(!window.m_restart);
        window.m_streaming = false;
        window.stopFollowing();
        RkmoonSessionControl::initialize();
    }
    void passwordOnlyReachesTheBoundTarget()
    {
        RkmoonAuth::clear();
        QVERIFY(!RkmoonAuth::hasPassword());
        QVERIFY(!RkmoonAuth::hasTarget());
        QVERIFY(RkmoonAuth::authorizationValue(TARGET_URL).isEmpty());

        // A password with no armed target can never be sent anywhere.
        RkmoonAuth::setPassword(QStringLiteral("kvm"));
        QVERIFY(RkmoonAuth::hasPassword());
        QVERIFY(!RkmoonAuth::hasTarget());
        QVERIFY(RkmoonAuth::authorizationValue(TARGET_URL).isEmpty());

        // An incomplete target must not arm either.
        RkmoonAuth::bindTarget(QStringLiteral("192.0.2.7"), 0);
        QVERIFY(!RkmoonAuth::hasTarget());
        RkmoonAuth::bindTarget(QString(), 47989);
        QVERIFY(!RkmoonAuth::hasTarget());
        QVERIFY(RkmoonAuth::authorizationValue(TARGET_URL).isEmpty());

        RkmoonAuth::bindTarget(QStringLiteral("192.0.2.7"), 47989);
        QVERIFY(RkmoonAuth::hasTarget());
        QVERIFY(RkmoonAuth::isBoundTarget(TARGET_URL));
        QCOMPARE(RkmoonAuth::authorizationValue(TARGET_URL), DEFAULT_AUTHORIZATION);

        // Another port, another host, another scheme and a port-less URL stay silent.
        QVERIFY(RkmoonAuth::authorizationValue(QUrl("http://192.0.2.7:47984/serverinfo")).isEmpty());
        QVERIFY(RkmoonAuth::authorizationValue(QUrl("http://192.0.2.8:47989/launch")).isEmpty());
        QVERIFY(RkmoonAuth::authorizationValue(QUrl("https://192.0.2.7:47989/launch")).isEmpty());
        QVERIFY(RkmoonAuth::authorizationValue(QUrl("http://192.0.2.7/serverinfo")).isEmpty());

        // Non-ASCII passwords are encoded as UTF-8 before base64, per the contract.
        RkmoonAuth::setPassword(QString::fromUtf8("p\xc3\xa4ssw\xc3\xb6rd"));
        QCOMPARE(RkmoonAuth::authorizationValue(TARGET_URL), QByteArray("Basic a3ZtOnDDpHNzd8O2cmQ="));

        // Disarming keeps what the operator typed but stops every request.
        RkmoonAuth::unbindTarget();
        QVERIFY(RkmoonAuth::hasPassword());
        QVERIFY(!RkmoonAuth::hasTarget());
        QVERIFY(RkmoonAuth::authorizationValue(TARGET_URL).isEmpty());

        RkmoonAuth::clear();
        QVERIFY(!RkmoonAuth::hasPassword());
        QVERIFY(!RkmoonAuth::hasTarget());
    }

    void preparedRequestCarriesCredentialAndBlocksRedirects()
    {
        RkmoonAuth::clear();
        RkmoonAuth::setPassword(QStringLiteral("kvm"));
        RkmoonAuth::bindTarget(QStringLiteral("192.0.2.7"), 47989);

        const QUrl target("http://192.0.2.7:47989/launch");
        QNetworkRequest request(target);
        RkmoonAuth::prepareRequest(request, target);
        QCOMPARE(request.rawHeader("Authorization"), DEFAULT_AUTHORIZATION);
        // Qt would otherwise follow a 3xx answer and replay this header at whatever host,
        // port or scheme the redirect names.
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
        // The credential is a header: the URL that upstream may log stays clean.
        QCOMPARE(request.url(), target);
        QVERIFY(!request.url().toString().contains(QStringLiteral("kvm")));

        const QUrl other("http://192.0.2.8:47989/launch");
        QNetworkRequest otherRequest(other);
        RkmoonAuth::prepareRequest(otherRequest, other);
        QVERIFY(otherRequest.rawHeader("Authorization").isEmpty());
        QCOMPARE(otherRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));

        const QUrl otherScheme("https://192.0.2.7:47989/serverinfo");
        QNetworkRequest otherSchemeRequest(otherScheme);
        RkmoonAuth::prepareRequest(otherSchemeRequest, otherScheme);
        QVERIFY(otherSchemeRequest.rawHeader("Authorization").isEmpty());

        RkmoonAuth::clear();
    }

    void passwordIsNeverPersisted()
    {
        RkmoonAuth::clear();
        RkmoonAuth::setPassword(QStringLiteral("SYNTHETIC_PASSWORD_VALUE"));
        KvmConfig config;
        config.address = QStringLiteral("192.0.2.7");
        config.hostName = QStringLiteral("synthetic-host");
        config.save();

        QSettings settings;
        settings.sync();
        const QStringList keys = settings.allKeys();
        QVERIFY(!keys.isEmpty());
        for (const QString& key : keys) {
            QVERIFY2(!key.contains(QStringLiteral("pass"), Qt::CaseInsensitive), qPrintable(key));
            QVERIFY2(!settings.value(key).toString().contains(QStringLiteral("SYNTHETIC_PASSWORD_VALUE")),
                     qPrintable(key));
        }
        RkmoonAuth::clear();
    }

    // Drives KvmHost against a loopback server this process starts: the authorized
    // serverinfo and applist exchange, the exact Authorization header on every request,
    // a rejected wrong password, and refusal of a host that does not speak the scheme.
    // No device or remote host is involved.
    void loopbackAuthenticateAndReject()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.handler = [](const QByteArray& path, const QByteArray& authorization) {
            if (authorization != DEFAULT_AUTHORIZATION) {
                return serverInfoXml(false);
            }
            if (path.startsWith("/applist")) {
                return appListXml();
            }
            return serverInfoXml(true);
        };

        QSettings().clear();
        KvmConfig config;
        config.address = QStringLiteral("127.0.0.1");
        config.httpPort = server.serverPort();
        config.save();
        KvmHost host(config);
        host.setPassword(QStringLiteral("kvm"));

        QString failure = runOffMainThread([&] { host.refresh(); host.hdmiApp(); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(config.hostUuid, QStringLiteral("SYNTHETIC-LOOPBACK-UUID"));
        QCOMPARE(config.appId, 881448767);
        // serverinfo plus applist, each carrying the credential; nothing went anywhere else.
        QVERIFY(server.requests >= 2);
        for (const QByteArray& value : server.seenAuthorization) {
            QCOMPARE(value, DEFAULT_AUTHORIZATION);
        }
        // applist is one of the upstream "HTTPS" call sites. Receiving it here proves
        // those call sites now reach the single plaintext base port. NvComputer still
        // substitutes upstream's default for the HttpsPort=0 the server reports, but that
        // value is inert because the patched NvHTTP::setHttpsPort ignores it.
        QVERIFY(server.seenPaths.contains(QByteArray("/serverinfo")));
        QVERIFY(server.seenPaths.contains(QByteArray("/applist")));

        // A wrong password is refused and leaves nothing armed.
        host.setPassword(QStringLiteral("not-the-host-password"));
        failure = runOffMainThread([&] { host.refresh(); });
        QVERIFY2(failure.contains(QStringLiteral("rejected the password")), qPrintable(failure));
        QVERIFY(!RkmoonAuth::hasTarget());

        // A host that authorizes but does not advertise the scheme is refused too.
        LoopbackServer foreign;
        QVERIFY(foreign.listen(QHostAddress::LocalHost, 0));
        foreign.handler = [](const QByteArray&, const QByteArray&) {
            QByteArray xml = serverInfoXml(true);
            xml.replace("<RKMoonAuth>password-http-v1</RKMoonAuth><RKMoonDisplayVersion>1</RKMoonDisplayVersion><RKMoonDisplayStatus>ready</RKMoonDisplayStatus><RKMoonDisplayWidth>2560</RKMoonDisplayWidth><RKMoonDisplayHeight>1440</RKMoonDisplayHeight><RKMoonDisplayFpsX100>8999</RKMoonDisplayFpsX100>", "");
            return xml;
        };
        KvmConfig foreignConfig;
        foreignConfig.address = QStringLiteral("127.0.0.1");
        foreignConfig.httpPort = foreign.serverPort();
        foreignConfig.hostUuid.clear();
        KvmHost foreignHost(foreignConfig);
        foreignHost.setPassword(QStringLiteral("kvm"));
        failure = runOffMainThread([&] { foreignHost.refresh(); });
        QVERIFY2(failure.contains(QStringLiteral("does not speak RKMoon password authentication")),
                 qPrintable(failure));
        QVERIFY(!RkmoonAuth::hasTarget());

        RkmoonAuth::clear();
    }

    void connectShowsInfoWithoutStreamingAndStartAcceptsNoSignal()
    {
        LoopbackServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        bool signal = true;
        server.handler = [&](const QByteArray& path, const QByteArray&) {
            if (path.startsWith("/applist")) return appListXml();
            auto body = serverInfoXml(true);
            if (!signal) body.replace("<RKMoonDisplayStatus>ready", "<RKMoonDisplayStatus>no_signal");
            return body;
        };
        KvmWindow window;
        window.m_config.hostUuid.clear();
        window.m_address->setText("127.0.0.1");
        window.m_port->setValue(server.serverPort());
        window.connectHost();
        QTRY_VERIFY(!window.m_connecting);
        QVERIFY(window.m_connected);
        QVERIFY(!window.m_wantStream);            // Connect never opens the stream.
        QVERIFY(window.m_poll.isActive());        // Info keeps updating.
        QCOMPARE(window.m_start->text(), QString("Start"));
        QVERIFY(window.m_start->isEnabled());
        QVERIFY(window.m_info->text().contains("rkmoon-loopback"));
        QVERIFY(window.m_info->text().contains("2560"));
        QVERIFY(window.m_info->text().contains("HEVC"));
        signal = false;
        window.pollDisplay(); QTRY_VERIFY(!window.m_pollBusy);
        QVERIFY(!window.m_wantStream);
        QVERIFY(window.m_info->text().contains("no signal"));
        QVERIFY(window.m_start->isEnabled());
        // Start is accepted without a signal; Cancel returns to the idle info view.
        window.startStream();
        QVERIFY(window.m_wantStream);
        QCOMPARE(window.m_start->text(), QString("Cancel"));
        window.startStream();
        QVERIFY(!window.m_wantStream);
        QVERIFY(window.m_poll.isActive());
        QCOMPARE(window.m_start->text(), QString("Start"));
        window.m_poll.stop();
        RkmoonAuth::clear();
    }

    void connectReentryAndCloseGuard()
    {
        KvmWindow window;
        // Set the same guard that surrounds a connection attempt. All attempted
        // reentry must return without touching the network or host ownership.
        window.m_connecting = true;
        window.m_start->setEnabled(true);
        window.connectHost();
        window.startStream();
        window.settings();
        QVERIFY(window.m_host.computer() == nullptr);
        QVERIFY(!window.m_streaming);
        QCloseEvent close;
        window.closeEvent(&close);
        QVERIFY(!close.isAccepted());
        window.m_connecting = false;
        window.m_streaming = true;
        QCloseEvent streamingClose;
        window.closeEvent(&streamingClose);
        QVERIFY(!streamingClose.isAccepted());
        window.m_streaming = false;
        QCloseEvent idleClose;
        window.closeEvent(&idleClose);
        QVERIFY(idleClose.isAccepted());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir privateSettings;
    if (!privateSettings.isValid()) return 1;
    QCoreApplication::setOrganizationName("RKMoon-offline-tests");
    QCoreApplication::setApplicationName("Isolated-client-test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, privateSettings.path());
    ClientUiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "client_ui.moc"
