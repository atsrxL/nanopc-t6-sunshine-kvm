// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmwindow.h"
#include "rkmoon_audio_control.h"
#include "rkmoon_audio_keys.h"
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
#include <QMetaObject>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <thread>
#include <exception>
#include <atomic>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <SDL.h>

namespace {
// Filter the reserved local controls so they cannot become remote key events.
// Ctrl+Alt+Shift+Q/X/Z remain upstream session quit/fullscreen/release shortcuts.
int SDLCALL localAudioKeys(void* context, SDL_Event* event)
{
    return static_cast<RkmoonAudioKeys*>(context)->filter(*event);
}
struct ScopedAudioFilter {
    RkmoonAudioKeys keys;
    ScopedAudioFilter() { SDL_SetEventFilter(localAudioKeys, &keys); }
    ~ScopedAudioFilter() { SDL_SetEventFilter(nullptr, nullptr); }
};
}

KvmWindow::KvmWindow() : m_host(m_config)
{
    setWindowTitle(tr("RKMoon HDMI KVM"));
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Dedicated HDMI host address (IP or DNS name):")));
    m_address = new QLineEdit(m_config.address, this);
    layout->addWidget(m_address);
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
        if (m_streaming || m_connecting) return;
        if (QMessageBox::question(this, tr("Forget binding"), tr("Forget the trusted host certificate and require PIN approval again?")) != QMessageBox::Yes) return;
        m_config.serverCertPem.clear();
        m_config.hostUuid.clear();
        m_config.hostName.clear();
        m_config.appId = 0;
        m_config.save();
        m_start->setEnabled(false);
        updateStatus(tr("Host binding forgotten. Revoke the old client on the host if needed."));
    });
    m_status = new QLabel(tr("Not connected"), this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addWidget(new QLabel(tr("During stream: Ctrl+Alt+Shift+Q exits; X toggles fullscreen; Z releases input.\n"
                                     "Ctrl+Alt+Shift+F9 mute, F10 volume down, F11 volume up."), this));
    connect(m_connect, &QPushButton::clicked, this, &KvmWindow::connectHost);
    connect(m_start, &QPushButton::clicked, this, &KvmWindow::startStream);
    connect(options, &QPushButton::clicked, this, &KvmWindow::settings);
    m_start->setEnabled(false);
    RkmoonAudioControl::setVolumePercent(m_config.volumePercent);
    RkmoonAudioControl::setMuted(m_config.muted);
}

void KvmWindow::updateStatus(const QString& status) { m_status->setText(status); }

void KvmWindow::closeEvent(QCloseEvent* event)
{
    // Never let QApplication terminate while the pairing thread or upstream
    // deferred session cleanup still references this window and host.
    if (m_connecting || m_streaming) event->ignore();
    else QWidget::closeEvent(event);
}

void KvmWindow::connectHost()
{
    if (m_streaming || m_connecting) return;
    QScopedValueRollback<bool> connecting(m_connecting, true);
    QString address = m_address->text().trimmed();
    // An address change must never reuse a previous host's pinned certificate or app ID.
    if (address != m_config.address) {
        m_config.address = address;
        m_config.hostUuid.clear();
        m_config.hostName.clear();
        m_config.serverCertPem.clear();
        m_config.appId = 0;
        m_config.save();
    }
    m_start->setEnabled(false);
    try {
        m_host.refresh();
        if (m_host.computer()->pairState != NvComputer::PS_PAIRED || m_config.serverCertPem.isEmpty()) {
            // The client supplies the PIN; the host operator approves it using the local
            // private admin socket. Waiting in the GUI thread would deadlock the flow.
            const QString pin = QString("%1").arg(QRandomGenerator::system()->bounded(10000), 4, 10, QChar('0'));
            QMessageBox notice(QMessageBox::Information, tr("Approve pairing on the host"),
                tr("Pairing PIN: %1\n\nOn the host, while this dialog is open:\n"
                   "1. Run: python3 tools/admin.py --state <private-state-dir> list\n"
                   "2. Run: python3 tools/admin.py --state <private-state-dir> pin --id <listed-id>\n"
                   "3. Enter the PIN shown above into the host terminal.\n\n"
                   "PIN is never sent to a web UI or saved by this client.").arg(pin),
                QMessageBox::NoButton, this);
            notice.setWindowModality(Qt::ApplicationModal);
            notice.setAttribute(Qt::WA_DeleteOnClose, false);
            notice.show();
            QEventLoop wait;
            std::exception_ptr failure;
            std::thread pairing([&, pin] {
                try { m_host.pair(pin); }
                catch (...) { failure = std::current_exception(); }
                QMetaObject::invokeMethod(&wait, [&] { notice.close(); wait.quit(); }, Qt::QueuedConnection);
            });
            wait.exec();
            pairing.join();
            if (failure) std::rethrow_exception(failure);
        }
        // Resolve only the fixed HDMI application, never show or manage an app library.
        m_host.hdmiApp();
        m_start->setEnabled(true);
        updateStatus(tr("Paired with %1; ready for HDMI video and stereo audio.").arg(m_host.computer()->name));
    }
    catch (const GfeHttpResponseException& e) { updateStatus(tr("Connect failed: %1").arg(e.toQString())); }
    catch (const QtNetworkReplyException& e) { updateStatus(tr("Connect failed: %1").arg(e.toQString())); }
    catch (const std::exception& e) {
        updateStatus(tr("Connect failed: %1").arg(QString::fromUtf8(e.what())));
    }
}

void KvmWindow::startStream()
{
    if (m_streaming || m_connecting || !m_start->isEnabled()) return;
    try {
        // Refresh server state and validate pinned identity before every launch.
        m_host.refresh();
        NvApp app = m_host.hdmiApp();
        auto* prefs = StreamingPreferences::get();
        m_config.applyTo(*prefs);
        Session session(m_host.computer(), app, prefs);
        connect(&session, &Session::stageFailed, this, [this](const QString& stage, int code, const QString&) {
            updateStatus(tr("Stream failed at %1 (%2)").arg(stage).arg(code));
        });
        connect(&session, &Session::displayLaunchError, this, [this](const QString& text) { updateStatus(text); });
        connect(&session, &Session::connectionStarted, this, [this] { updateStatus(tr("HDMI stream connected")); });
        // Upstream LiStopConnection/quitApp runs in DeferredSessionCleanupTask after
        // exec() returns. The Session and NvComputer must outlive readyForDeletion.
        QEventLoop cleanupWait;
        bool cleanupReady = false;
        connect(&session, &Session::readyForDeletion, &cleanupWait, [&] {
            cleanupReady = true;
            cleanupWait.quit();
        });
        m_streaming = true;
        m_start->setEnabled(false);
        m_connect->setEnabled(false);
        hide();
        ScopedAudioFilter audioFilter;
        session.exec(windowHandle()); // Upstream SDL loop owns the foreground until exit.
        if (!cleanupReady) cleanupWait.exec(); // Do not destroy Session during async cleanup.
        SDL_SetEventFilter(nullptr, nullptr);
        show();
        m_streaming = false;
        m_connect->setEnabled(true);
        m_start->setEnabled(true);
        m_config.volumePercent = RkmoonAudioControl::volumePercent();
        m_config.muted = RkmoonAudioControl::isMuted();
        m_config.save();
        if (m_status->text() == tr("HDMI stream connected")) updateStatus(tr("Stream ended; reconnect to resume."));
    }
    catch (const GfeHttpResponseException& e) {
        updateStatus(tr("Stream refused: %1").arg(e.toQString()));
    }
    catch (const QtNetworkReplyException& e) {
        updateStatus(tr("Stream refused: %1").arg(e.toQString()));
    }
    catch (const std::exception& e) {
        show();
        m_streaming = false;
        m_connect->setEnabled(true);
        updateStatus(tr("Stream refused: %1").arg(QString::fromUtf8(e.what())));
    }
}

void KvmWindow::settings()
{
    if (m_streaming || m_connecting) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Essential stream settings"));
    QFormLayout form(&dialog);
    QSpinBox width; width.setRange(640, 3840); width.setValue(m_config.width);
    QSpinBox height; height.setRange(480, 2160); height.setValue(m_config.height);
    QSpinBox fps; fps.setRange(24, 120); fps.setValue(m_config.fps);
    QSpinBox bitrate; bitrate.setRange(1000, 80000); bitrate.setValue(m_config.bitrateKbps);
    QComboBox codec; codec.addItems({tr("Auto (HEVC preferred)"), tr("HEVC"), tr("H.264")}); codec.setCurrentIndex(m_config.codec);
    QCheckBox fullscreen(tr("Fullscreen")); fullscreen.setChecked(m_config.fullScreen);
    QCheckBox vsync(tr("VSync")); vsync.setChecked(m_config.vsync);
    QSpinBox volume; volume.setRange(0, 100); volume.setSuffix("%"); volume.setValue(m_config.volumePercent);
    QCheckBox muted(tr("Mute playback")); muted.setChecked(m_config.muted);
    form.addRow(tr("Width"), &width); form.addRow(tr("Height"), &height);
    form.addRow(tr("FPS"), &fps); form.addRow(tr("Bitrate kbps"), &bitrate);
    form.addRow(tr("Codec"), &codec); form.addRow(&fullscreen); form.addRow(&vsync);
    form.addRow(tr("Volume"), &volume); form.addRow(&muted);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    m_config.width = width.value(); m_config.height = height.value(); m_config.fps = fps.value();
    m_config.bitrateKbps = bitrate.value(); m_config.codec = static_cast<KvmConfig::Codec>(codec.currentIndex());
    m_config.fullScreen = fullscreen.isChecked(); m_config.vsync = vsync.isChecked();
    m_config.volumePercent = volume.value(); m_config.muted = muted.isChecked();
    RkmoonAudioControl::setVolumePercent(m_config.volumePercent);
    RkmoonAudioControl::setMuted(m_config.muted);
    m_config.save();
}
