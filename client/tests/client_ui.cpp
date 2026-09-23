// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "kvmwindow.h"
#include "rkmoon_audio_keys.h"
#include "rkmoon_audio_control.h"
#include "rkmoon_http_log.h"
#include "settings/streamingpreferences.h"

#include <QApplication>
#include <QCloseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QStringList captured;
void capture(QtMsgType, const QMessageLogContext&, const QString& text) { captured.append(text); }
}

class ClientUiTest : public QObject
{
    Q_OBJECT
private slots:
    void redactedHttpLogs()
    {
        captured.clear();
        const auto previous = qInstallMessageHandler(capture);
        // Synthetic sentinels only. No real key, cert, PIN or network operation.
        rkmoonLogHttpRequest("launch");
        rkmoonLogHttpRequest("https://invalid/launch?rikey=SYNTHETIC_RIKEY&clientcert=SYNTHETIC_CERT");
        rkmoonLogHttpRequest("pair?clientsecret=SYNTHETIC_PAIR_SECRET", true);
        rkmoonLogHttpRequest("resume", true);
        qInstallMessageHandler(previous);
        QCOMPARE(captured.size(), 4);
        const auto joined = captured.join('\n');
        QVERIFY(joined.contains("launch"));
        QVERIFY(joined.contains("resume"));
        QVERIFY(!joined.contains("SYNTHETIC"));
        QVERIFY(!joined.contains("rikey"));
        QVERIFY(!joined.contains("clientcert"));
        QVERIFY(!joined.contains("clientsecret"));
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
        QVERIFY(!preferences->absoluteMouseMode);
        QVERIFY(!preferences->absoluteTouchMode);
        QVERIFY(!preferences->multiController);
        QVERIFY(!preferences->enableMdns);
        QCOMPARE(preferences->audioConfig, StreamingPreferences::AC_STEREO);
        QCOMPARE(preferences->videoDecoderSelection, StreamingPreferences::VDS_FORCE_HARDWARE);
    }
    void pairingReentryAndCloseGuard()
    {
        KvmWindow window;
        // Set the same guard that surrounds the pairing worker. All attempted
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
