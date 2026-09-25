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
#include <Limelight.h>
#include <algorithm>

namespace {
// Reconnect buffering. Transient serverinfo transport failures are tolerated for about
// ten one-second polls during playback/recovery; an unexpected session end is retried
// with 1/2/4/8/8 s backoff; a new source mode must be seen on two consecutive polls.
constexpr int POLL_FAILURE_LIMIT = 10;
constexpr int RECONNECT_LIMIT = 5;
constexpr int RECONNECT_MAX_DELAY_MS = 8000;
constexpr int MODE_STABLE_POLLS = 2;
// With no HDMI source the server streams hardware-encoded black (ADR-011) so the keyboard
// and mouse can wake the target; the session restarts in the real mode once it appears.
constexpr int PLACEHOLDER_WIDTH = 1920;
constexpr int PLACEHOLDER_HEIGHT = 1080;
constexpr int PLACEHOLDER_FPS = 60;
bool canStart(const RkmoonDisplay& mode) { return mode.ready() || mode.status == QLatin1String("no_signal"); }

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
    m_reconnectTimer.setSingleShot(true);
    RkmoonDiagnostics::record("window-created");
    connect(&m_poll, &QTimer::timeout, this, &KvmWindow::pollDisplay);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &KvmWindow::reconnectNow);
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
    m_start = new QPushButton(tr("Start"), this);
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
        m_poll.stop();
        m_connected = false;
        m_host.forget();
        m_start->setEnabled(false);
        updateInfo();
        updateStatus(tr("Host binding forgotten."));
    });
    m_info = new QLabel(this);
    m_info->setObjectName("serverInfo");
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setFrameShape(QFrame::StyledPanel);
    m_info->setMargin(6);
    layout->addWidget(m_info);
    updateInfo();
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
    RkmoonDiagnostics::record(QString("qt-close busy=%1").arg(m_connecting || m_streaming || m_pollBusy));
    // Never let QApplication terminate while a connection attempt or upstream
    // deferred session cleanup still references this window and host.
    if (m_connecting || m_streaming || m_pollBusy) event->ignore();
    else { stopFollowing(); m_poll.stop(); QWidget::closeEvent(event); }
}

// Stops any wish to stream (waiting, reconnecting, restarting) and the poll timer;
// callers that stay connected resume info polling with resumeInfoPolling().
void KvmWindow::stopFollowing()
{
    m_wantStream = false;
    m_restart = false;
    m_poll.stop();
    m_reconnectTimer.stop();
    m_reconnectPending = false;
    m_reconnectAttempt = 0;
    m_pendingModePolls = 0;
}

void KvmWindow::resumeInfoPolling()
{
    if (m_connected && !m_poll.isActive()) m_poll.start();
}

void KvmWindow::updateInfo()
{
    if (!m_connected || !m_host.computer()) {
        m_info->setText(tr("No server information. Enter the host address and click Bind / Connect."));
        return;
    }
    auto* host = m_host.computer();
    const auto& mode = m_host.display;
    QString hdmi;
    if (mode.ready()) hdmi = tr("%1 × %2 @ %3 Hz").arg(mode.width).arg(mode.height).arg(mode.fpsX100 / 100.0, 0, 'f', 2);
    else if (mode.status == QLatin1String("no_signal"))
        hdmi = tr("no signal (Start opens a black screen; keyboard/mouse can wake the target)");
    else if (mode.status == QLatin1String("unsupported")) hdmi = tr("source mode not supported by the capture");
    else hdmi = tr("capture device unavailable");
    QStringList mice;
    if (m_host.supportsAbsoluteMouse) mice << tr("absolute");
    if (m_host.supportsRelativeMouse) mice << tr("relative");
    QStringList codecs;
    if (host->serverCodecModeSupport & SCM_H264) codecs << QStringLiteral("H.264");
    if (host->serverCodecModeSupport & SCM_HEVC) codecs << QStringLiteral("HEVC");
    QString text = tr("Host: %1\nID: %2\nAddress: %3:%4\nHDMI: %5\nMouse modes: %6\nCodecs: %7")
        .arg(host->name, host->uuid, m_config.address).arg(m_config.httpPort)
        .arg(hdmi, mice.isEmpty() ? tr("none") : mice.join(", "), codecs.isEmpty() ? tr("unknown") : codecs.join(", "));
    if (m_unreachable) text += tr("\n(Host not answering; retrying — values above may be stale)");
    m_info->setText(text);
}

void KvmWindow::connectHost()
{
    if (m_streaming || m_connecting || m_pollBusy) return;
    stopFollowing();
    m_poll.stop();
    m_connected = false;
    m_unreachable = false;
    if (m_config.address != m_address->text().trimmed() || m_config.httpPort != m_port->value()) m_host.forget();
    m_config.address = m_address->text().trimmed();
    m_config.httpPort = static_cast<quint16>(m_port->value());
    m_host.setPassword(m_password->text());
    RkmoonAuth::bindTarget(m_config.address, m_config.httpPort);
    m_connecting = true;
    updateMouseModeEnabled();
    m_start->setEnabled(false);
    updateInfo();
    updateStatus(tr("Connecting…"));
    m_host.refreshAsync([this](QString error) {
        if (!error.isEmpty()) {
            m_connecting = false;
            updateStatus(error);
            return;
        }
        m_host.refreshAsync([this](QString appError) {
            m_connecting = false;
            if (!appError.isEmpty()) { updateStatus(appError); return; }
            // Connected: show what the server reports and keep it current. Streaming
            // starts only when the user clicks Start.
            m_connected = true;
            m_pollFailures = 0;
            m_start->setEnabled(true);
            m_start->setText(tr("Start"));
            updateInfo();
            updateStatus(tr("Connected. Click Start to open the remote screen."));
            m_poll.start();
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
            // Only transport-level failures while playing or recovering are buffered;
            // authentication, identity and protocol rejections stop immediately.
            if (m_host.transientFailure && m_wantStream && !m_streaming && m_reconnectAttempt > 0) {
                // Recovering after an unexpected end: the backoff schedule owns the
                // retry budget. Waiting polls are ignored; a failed attempt advances it.
                if (!m_reconnectPending) { m_sessionError = error; scheduleReconnect(); }
                return;
            }
            if (m_host.transientFailure && m_wantStream && m_streaming && m_pollFailures < POLL_FAILURE_LIMIT) {
                updateStatus(tr("Display status temporarily unavailable; retrying (%1/%2)").arg(m_pollFailures).arg(POLL_FAILURE_LIMIT));
                return;
            }
            if (m_host.transientFailure && !m_wantStream && !m_streaming) {
                // Idle info view: keep the last values and keep asking.
                m_unreachable = true;
                updateInfo();
                return;
            }
            m_sessionError = error;
            stopFollowing();
            m_poll.stop();
            m_connected = false;
            updateStatus(error);
            m_start->setEnabled(false);
            updateInfo();
            if (m_streaming) { RkmoonSessionControl::stop(); }
            return;
        }
        m_pollFailures = 0;
        m_unreachable = false;
        updateInfo();
        const auto mode = m_host.display;
        m_start->setText(m_wantStream && !m_streaming ? tr("Cancel") : tr("Start"));
        if (m_wantStream && !m_streaming && m_sessionError.isEmpty() && !canStart(mode))
            updateStatus(tr("Waiting for the capture: %1").arg(mode.status));
        if (RkmoonSessionControl::userQuit.load() && m_streaming) m_wantStream = false;
        if (m_streaming && m_wantStream && !m_cleanup && !m_restart) {
            if (mode == m_runningMode) { m_pendingModePolls = 0; return; }
            // Debounce HDMI switching: restart only after the same new mode is
            // reported by consecutive polls.
            if (m_pendingModePolls > 0 && mode == m_pendingMode) ++m_pendingModePolls;
            else { m_pendingMode = mode; m_pendingModePolls = 1; }
            RkmoonDiagnostics::record(QString("display-change-seen polls=%1 old=%2x%3/%4 new=%5x%6/%7")
                .arg(m_pendingModePolls).arg(m_runningMode.width).arg(m_runningMode.height).arg(m_runningMode.fpsX100)
                .arg(mode.width).arg(mode.height).arg(mode.fpsX100));
            if (m_pendingModePolls < MODE_STABLE_POLLS) return;
            m_pendingModePolls = 0;
            m_restart = true;
            RkmoonDiagnostics::record("display-change-restart");
            RkmoonSessionControl::stop();
        } else if (!m_streaming && m_wantStream && !m_reconnectPending && canStart(mode)) {
            QTimer::singleShot(0, this, &KvmWindow::runStream);
        }
    });
}

void KvmWindow::startStream()
{
    if (m_streaming || m_connecting || !m_start->isEnabled()) return;
    if (m_wantStream) {
        stopFollowing(); resumeInfoPolling();
        m_start->setText(tr("Start"));
        updateStatus(tr("Cancelled"));
        return;
    }
    stopFollowing();
    m_wantStream = true;
    m_start->setText(tr("Cancel"));
    updateStatus(tr("Starting…"));
    m_poll.start();
    pollDisplay();
}

void KvmWindow::markConnected()
{
    RkmoonDiagnostics::record(QString("connection-started reconnectAttempt=%1").arg(m_reconnectAttempt));
    m_sessionConnected = true;
    m_reconnectAttempt = 0;
    m_pollFailures = 0;
    m_reconnectPending = false;
    m_reconnectTimer.stop();
    updateStatus(tr("HDMI stream connected"));
}

void KvmWindow::sessionEnded()
{
    if (RkmoonSessionControl::userQuit.load()) m_wantStream = false;
    const bool restart = m_restart;
    m_restart = false;
    m_pendingModePolls = 0;
    if (!m_wantStream) {
        // User exit, authentication/identity/protocol stop, or cancelled follow.
        m_reconnectTimer.stop();
        m_reconnectPending = false;
        m_reconnectAttempt = 0;
        resumeInfoPolling();
        m_start->setText(tr("Start"));
        if (m_status->text() == tr("HDMI stream connected")) updateStatus(tr("Stream ended; click Start to resume."));
        return;
    }
    if (restart) return; // Confirmed source mode change: the next ready poll relaunches.
    if (!m_sessionConnected && m_reconnectAttempt == 0) {
        // First launch never connected: report the launch failure, do not loop.
        RkmoonDiagnostics::record("launch-ended-before-connect");
        stopFollowing();
        resumeInfoPolling();
        m_start->setText(tr("Start"));
        if (m_sessionError.isEmpty()) updateStatus(tr("Stream ended; click Start to retry."));
        return;
    }
    scheduleReconnect();
}

void KvmWindow::launchFailed(const QString& status, bool transport)
{
    show(); m_streaming = false; m_host.streaming = false; m_cleanup = false;
    m_connect->setEnabled(true); m_start->setEnabled(true);
    updateMouseModeEnabled();
    m_sessionError = status;
    // A transport failure while already recovering continues the backoff; any
    // HTTP/protocol refusal (including authentication) stops following.
    if (transport && m_wantStream && m_reconnectAttempt > 0) { scheduleReconnect(); return; }
    stopFollowing();
    resumeInfoPolling();
    m_start->setText(tr("Start"));
    updateStatus(status);
}

void KvmWindow::scheduleReconnect()
{
    if (m_reconnectAttempt >= RECONNECT_LIMIT) {
        RkmoonDiagnostics::record(QString("reconnect-exhausted attempts=%1").arg(m_reconnectAttempt));
        const QString detail = m_sessionError;
        stopFollowing();
        resumeInfoPolling();
        m_start->setText(tr("Start"));
        updateStatus(QString::fromUtf16(u"\u8fde\u63a5\u4e2d\u65ad\uff0c\u81ea\u52a8\u91cd\u8fde %1 \u6b21\u672a\u6210\u529f\uff1b\u8bf7\u70b9\u51fb Start \u91cd\u8bd5\u3002").arg(RECONNECT_LIMIT)
                     + (detail.isEmpty() ? QString() : QStringLiteral("\n") + detail));
        return;
    }
    ++m_reconnectAttempt;
    const int delay = std::min(1000 << (m_reconnectAttempt - 1), RECONNECT_MAX_DELAY_MS);
    RkmoonDiagnostics::record(QString("reconnect-scheduled attempt=%1 delayMs=%2").arg(m_reconnectAttempt).arg(delay));
    m_reconnectPending = true;
    m_reconnectTimer.start(delay);
    if (!m_poll.isActive()) m_poll.start();
    m_start->setText(tr("Cancel"));
    m_start->setEnabled(true);
    updateStatus(QString::fromUtf16(u"\u8fde\u63a5\u4e2d\u65ad\uff0c\u6b63\u5728\u91cd\u8fde\uff08%1/%2\uff09").arg(m_reconnectAttempt).arg(RECONNECT_LIMIT)
                 + (m_sessionError.isEmpty() ? QString() : QStringLiteral("\n") + m_sessionError));
}

void KvmWindow::reconnectNow()
{
    if (!m_wantStream || m_streaming || !m_reconnectPending) return;
    m_reconnectPending = false;
    RkmoonDiagnostics::record(QString("reconnect-attempt attempt=%1").arg(m_reconnectAttempt));
    pollDisplay(); // Re-validates password/identity/mode before relaunch.
}

void KvmWindow::runStream()
{
    if (m_streaming || m_connecting || !m_wantStream || !canStart(m_host.display)) return;
    try {
        // Refresh server state and validate pinned identity before every launch.
        const bool absolute = m_config.mouseMode == KvmConfig::MOUSE_ABSOLUTE;
        if ((absolute && !m_host.supportsAbsoluteMouse) || (!absolute && !m_host.supportsRelativeMouse)) {
            stopFollowing();
            resumeInfoPolling();
            m_start->setText(tr("Start"));
            updateStatus(tr("Server does not advertise the selected mouse mode; upgrade/configure its input support or choose a supported mode."));
            return;
        }
        RkmoonAuth::setMouseMode(absolute);
        NvApp app = m_host.selectedApp;
        m_runningMode = m_host.display;
        if (m_runningMode.ready()) {
            m_config.width = m_runningMode.width;
            m_config.height = m_runningMode.height;
            m_config.fps = (m_runningMode.fpsX100 + 50) / 100;
        } else {
            // No source: the server sends black at this size until a signal appears.
            m_config.width = PLACEHOLDER_WIDTH;
            m_config.height = PLACEHOLDER_HEIGHT;
            m_config.fps = PLACEHOLDER_FPS;
        }
        m_restart = false;
        m_sessionConnected = false;
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
        connect(&session, &Session::connectionStarted, this, [this] { markConnected(); });
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
        m_connect->setEnabled(true);
        m_start->setEnabled(true);
        m_config.volumePercent = RkmoonAudioControl::volumePercent();
        m_config.muted = RkmoonAudioControl::isMuted();
        m_config.save();
        sessionEnded();
    }
    catch (const GfeHttpResponseException& e) {
        launchFailed(tr("Stream refused: %1").arg(e.toQString()), false);
    }
    catch (const QtNetworkReplyException& e) {
        launchFailed(tr("Stream refused: %1").arg(e.toQString()), true);
    }
    catch (const std::exception& e) {
        launchFailed(tr("Stream refused: %1").arg(QString::fromUtf8(e.what())), false);
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
