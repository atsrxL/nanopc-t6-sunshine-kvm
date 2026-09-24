// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmwindow.h"
#include "rkmoon_audio_control.h"
#include "rkmoon_audio_keys.h"
#include "rkmoon_auth.h"
#include "rkmoon_session_control.h"
#include "rkmoon_diagnostics.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QEventLoop>
#include <QScopedValueRollback>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <SDL.h>

namespace {
// Filter the reserved local controls so they cannot become remote key events.
// Ctrl+Alt+Shift+Q/X/Z remain upstream session quit/fullscreen/release shortcuts.
int SDLCALL localAudioKeys(void* context, SDL_Event* event)
{
    RkmoonSessionControl::observe(*event);
    return static_cast<RkmoonAudioKeys*>(context)->filter(*event);
}
struct ScopedAudioFilter {
    RkmoonAudioKeys keys;
    ScopedAudioFilter() { RkmoonSessionControl::initialize(); SDL_SetEventFilter(localAudioKeys, &keys); }
    ~ScopedAudioFilter() { SDL_SetEventFilter(nullptr, nullptr); }
};
}

KvmWindow::KvmWindow() : m_host(m_config)
{
    m_poll.setInterval(1000);
    RkmoonDiagnostics::record("window-created");
    connect(&m_poll, &QTimer::timeout, this, &KvmWindow::pollDisplay);
    setWindowTitle(tr("RKMoon HDMI KVM"));
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Dedicated HDMI host address (IP or DNS name) and port:")));
    auto* target = new QHBoxLayout;
    m_address = new QLineEdit(m_config.address, this);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(m_config.httpPort);
    target->addWidget(m_address, 1);
    target->addWidget(m_port);
    layout->addLayout(target);
    layout->addWidget(new QLabel(tr("Host password (sent over plaintext HTTP; trusted networks only):")));
    // The password is never stored by this client, so the field always starts at the
    // server's default and is masked. It is held in memory only while the app runs.
    m_password = new QLineEdit(QStringLiteral("kvm"), this);
    m_password->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_password);
    auto* mouseRow = new QHBoxLayout;
    mouseRow->addWidget(new QLabel(tr("Mouse mode:"), this));
    m_mouseMode = new QComboBox(this);
    m_mouseMode->setObjectName("mouseMode");
    m_mouseMode->addItems({tr("Absolute — desktop / direct touch"), tr("Relative — touch trackpad")});
    m_mouseMode->setCurrentIndex(m_config.mouseMode);
    mouseRow->addWidget(m_mouseMode, 1);
    layout->addLayout(mouseRow);
    connect(m_mouseMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int mode) {
        if (m_streaming || m_connecting || m_pollBusy || m_cleanup) {
            QSignalBlocker blocker(m_mouseMode);
            m_mouseMode->setCurrentIndex(m_config.mouseMode);
            return;
        }
        m_config.mouseMode = static_cast<KvmConfig::MouseMode>(mode);
        m_config.save();
    });
    auto* row = new QHBoxLayout;
    m_connect = new QPushButton(tr("Bind / Connect"), this);
    m_start = new QPushButton(tr("View HDMI"), this);
    auto* options = new QPushButton(tr("Settings"), this);
    row->addWidget(m_connect);
    row->addWidget(m_start);
    row->addWidget(options);
    layout->addLayout(row);
    auto* forget = new QPushButton(tr("Forget host binding"), this);
    layout->addWidget(forget);
    connect(forget, &QPushButton::clicked, this, [this] {
        if (m_streaming || m_connecting || m_pollBusy) return;
        if (QMessageBox::question(this, tr("Forget binding"),
                                  tr("Forget the stored host identity and resolved HDMI app?"))
            != QMessageBox::Yes) return;
        stopFollowing();
        m_host.forget();
        m_start->setEnabled(false);
        updateStatus(tr("Host binding forgotten."));
    });
    m_status = new QLabel(tr("Not connected"), this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addWidget(new QLabel(tr("During stream: Ctrl+Alt+Shift+Q exits; X toggles fullscreen; Z releases input; C toggles local cursor.\n"
                                     "Ctrl+Alt+Shift+F9 mute, F10 volume down, F11 volume up."), this));
    connect(m_connect, &QPushButton::clicked, this, &KvmWindow::connectHost);
    connect(m_start, &QPushButton::clicked, this, &KvmWindow::startStream);
    connect(options, &QPushButton::clicked, this, &KvmWindow::settings);
    m_start->setEnabled(false);
    RkmoonAudioControl::setVolumePercent(m_config.volumePercent);
    RkmoonAudioControl::setMuted(m_config.muted);
}

KvmWindow::~KvmWindow()
{
    // Do not leave the password or the armed target behind once the window is gone.
    RkmoonAuth::clear();
}

void KvmWindow::updateMouseModeEnabled()
{
    m_mouseMode->setEnabled(!m_streaming && !m_connecting && !m_pollBusy && !m_cleanup);
}
void KvmWindow::updateStatus(const QString& status) { m_status->setText(status); updateMouseModeEnabled(); }

void KvmWindow::closeEvent(QCloseEvent* event)
{
    // Never let QApplication terminate while a connection attempt or upstream
    // deferred session cleanup still references this window and host.
    if (m_connecting || m_streaming || m_pollBusy) event->ignore();
    else { stopFollowing(); QWidget::closeEvent(event); }
}

void KvmWindow::stopFollowing()
{
    m_wantStream = false;
    m_restart = false;
    m_poll.stop();
}

void KvmWindow::connectHost()
{
    if (m_streaming || m_connecting || m_pollBusy) return;
    stopFollowing();
    if (m_config.address != m_address->text().trimmed() || m_config.httpPort != m_port->value()) m_host.forget();
    m_config.address = m_address->text().trimmed();
    m_config.httpPort = static_cast<quint16>(m_port->value());
    m_host.setPassword(m_password->text());
    RkmoonAuth::bindTarget(m_config.address, m_config.httpPort);
    m_connecting = true;
    updateMouseModeEnabled();
    m_start->setEnabled(false);
    m_host.refreshAsync([this](QString error) {
        if (!error.isEmpty()) {
            m_connecting = false;
            updateStatus(error);
            return;
        }
        m_host.refreshAsync([this](QString appError) {
            m_connecting = false;
            if (!appError.isEmpty()) { updateStatus(appError); return; }
            m_start->setEnabled(true);
            m_wantStream = true;
            m_disconnectChecks = 0;
            m_poll.start();
            pollDisplay();
        }, true);
    });
}

void KvmWindow::pollDisplay()
{
    if (m_pollBusy || m_connecting) return;
    m_pollBusy = true;
    updateMouseModeEnabled();
    m_host.refreshAsync([this](QString error) {
        m_pollBusy = false;
        if (!error.isEmpty()) {
            ++m_pollFailures;
            RkmoonDiagnostics::record(QString("poll-failure transient=%1 count=%2 streaming=%3 cleanup=%4")
                .arg(m_host.transientFailure).arg(m_pollFailures).arg(m_streaming).arg(m_cleanup));
            if (m_host.transientFailure && m_streaming && !m_cleanup && m_pollFailures < 3) {
                updateStatus(tr("Display status temporarily unavailable; retrying (%1/3)").arg(m_pollFailures));
                return;
            }
            m_sessionError = error;
            stopFollowing();
            updateStatus(error);
            m_start->setEnabled(false);
            if (m_streaming) { RkmoonSessionControl::stop(); }
            return;
        }
        m_pollFailures = 0;
        const auto mode = m_host.display;
        if (!mode.ready()) m_disconnectChecks = 0;
        m_start->setText(m_wantStream && !m_streaming ? tr("Cancel / Stop waiting") : tr("View HDMI"));
        if (m_sessionError.isEmpty()) updateStatus(mode.ready() ? tr("HDMI %1 × %2 @ %3 Hz").arg(mode.width).arg(mode.height).arg(mode.fpsX100 / 100.0)
                                 : tr("Waiting for HDMI: %1").arg(mode.status));
        if (RkmoonSessionControl::userQuit.load() && m_streaming) m_wantStream = false;
        if (m_streaming && m_wantStream && !m_cleanup && mode != m_runningMode && !m_restart) {
            m_restart = m_wantStream;
            RkmoonDiagnostics::record(QString("display-change old=%1x%2/%3 new=%4x%5/%6")
                .arg(m_runningMode.width).arg(m_runningMode.height).arg(m_runningMode.fpsX100)
                .arg(mode.width).arg(mode.height).arg(mode.fpsX100));
            RkmoonSessionControl::stop();
        } else if (!m_streaming && m_wantStream && mode.ready()) {
            if (m_disconnectChecks > 0 && mode == m_runningMode) {
                if (--m_disconnectChecks == 0) {
                    m_wantStream = false;
                    if (m_sessionError.isEmpty()) updateStatus(tr("Stream ended; click View HDMI to retry."));
                }
                return;
            }
            m_disconnectChecks = 0;
            QTimer::singleShot(0, this, &KvmWindow::runStream);
        }
    });
}

void KvmWindow::startStream()
{
    if (m_streaming || m_connecting || !m_start->isEnabled()) return;
    if (m_wantStream) { stopFollowing(); updateStatus(tr("Automatic connection cancelled")); return; }
    m_disconnectChecks = 0;
    m_wantStream = true;
    m_poll.start();
    pollDisplay();
}

void KvmWindow::runStream()
{
    if (m_streaming || m_connecting || !m_wantStream || !m_host.display.ready()) return;
    try {
        // Refresh server state and validate pinned identity before every launch.
        const bool absolute = m_config.mouseMode == KvmConfig::MOUSE_ABSOLUTE;
        if ((absolute && !m_host.supportsAbsoluteMouse) || (!absolute && !m_host.supportsRelativeMouse)) {
            stopFollowing();
            updateStatus(tr("Server does not advertise the selected mouse mode; upgrade/configure its input support or choose a supported mode."));
            return;
        }
        RkmoonAuth::setMouseMode(absolute);
        NvApp app = m_host.selectedApp;
        m_runningMode = m_host.display;
        m_config.width = m_runningMode.width;
        m_config.height = m_runningMode.height;
        m_config.fps = (m_runningMode.fpsX100 + 50) / 100;
        m_restart = false;
        m_sessionError.clear();
        RkmoonDiagnostics::record(QString("session-start currentGameId=%1 appId=%2 width=%3 height=%4 fps=%5")
            .arg(m_host.computer()->currentGameId).arg(app.id).arg(m_config.width).arg(m_config.height).arg(m_config.fps));
        auto* prefs = StreamingPreferences::get();
        m_config.applyTo(*prefs);
        Session session(m_host.computer(), app, prefs);
        connect(&session, &Session::stageFailed, this, [this](const QString& stage, int code, const QString&) {
            m_sessionError = tr("Stream failed at %1 (%2)").arg(stage).arg(code);
            RkmoonDiagnostics::record(QString("stage-failed code=%1").arg(code));
            updateStatus(m_sessionError);
        });
        connect(&session, &Session::displayLaunchError, this, [this](const QString& text) { m_sessionError = text; RkmoonDiagnostics::record("session-error-signal"); updateStatus(text); });
        connect(&session, &Session::connectionStarted, this, [this] { updateStatus(tr("HDMI stream connected")); });
        // Upstream LiStopConnection/quitApp runs in DeferredSessionCleanupTask after
        // exec() returns. The Session and NvComputer must outlive readyForDeletion.
        QEventLoop cleanupWait;
        bool cleanupReady = false;
        connect(&session, &Session::readyForDeletion, &cleanupWait, [&] {
            cleanupReady = true;
            cleanupWait.quit();
        });
        m_host.streaming = true;
        m_streaming = true;
        updateMouseModeEnabled();
        m_start->setEnabled(false);
        m_connect->setEnabled(false);
        hide();
        ScopedAudioFilter audioFilter;
        session.exec(windowHandle()); // Upstream SDL loop owns the foreground until exit.
        m_cleanup = true;
        RkmoonDiagnostics::record(QString("session-exec-return userQuit=%1 restart=%2 want=%3")
            .arg(RkmoonSessionControl::userQuit.load()).arg(m_restart).arg(m_wantStream));
        if (!cleanupReady) cleanupWait.exec();
        RkmoonDiagnostics::record("session-cleanup-ready");
        m_cleanup = false; // Do not destroy Session during async cleanup.
        SDL_SetEventFilter(nullptr, nullptr);
        show();
        m_host.streaming = false;
        m_streaming = false;
        updateMouseModeEnabled();
        if (RkmoonSessionControl::userQuit.load()) m_wantStream = false;
        m_disconnectChecks = m_wantStream && !m_restart ? 3 : 0;
        m_restart = false;
        m_connect->setEnabled(true);
        m_start->setEnabled(true);
        m_config.volumePercent = RkmoonAudioControl::volumePercent();
        m_config.muted = RkmoonAudioControl::isMuted();
        m_config.save();
        if (m_status->text() == tr("HDMI stream connected")) updateStatus(tr("Stream ended; reconnect to resume."));
    }
    catch (const GfeHttpResponseException& e) {
        show(); m_streaming = false; m_host.streaming = false; m_cleanup = false;
        m_connect->setEnabled(true); m_start->setEnabled(true);
        m_wantStream = false;
        updateStatus(tr("Stream refused: %1").arg(e.toQString()));
    }
    catch (const QtNetworkReplyException& e) {
        show(); m_streaming = false; m_host.streaming = false; m_cleanup = false;
        m_connect->setEnabled(true); m_start->setEnabled(true);
        m_wantStream = false;
        updateStatus(tr("Stream refused: %1").arg(e.toQString()));
    }
    catch (const std::exception& e) {
        show();
        m_cleanup = false; m_start->setEnabled(true);
        m_host.streaming = false;
        m_streaming = false;
        updateMouseModeEnabled();
        m_connect->setEnabled(true);
        m_wantStream = false;
        updateStatus(tr("Stream refused: %1").arg(QString::fromUtf8(e.what())));
    }
}

void KvmWindow::settings()
{
    if (m_streaming || m_connecting || m_pollBusy) return;
    QScopedValueRollback<bool> settingGuard(m_connecting, true);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Essential stream settings"));
    QFormLayout form(&dialog);
    QSpinBox bitrate; bitrate.setRange(1000, 35000); bitrate.setValue(m_config.bitrateKbps);
    QComboBox codec; codec.addItems({tr("Auto (HEVC preferred)"), tr("HEVC"), tr("H.264")}); codec.setCurrentIndex(m_config.codec);
    form.addRow(tr("Bitrate kbps"), &bitrate);
    form.addRow(tr("Codec"), &codec);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_config.bitrateKbps = bitrate.value();
    m_config.codec = static_cast<KvmConfig::Codec>(codec.currentIndex());
    m_config.save();
}
