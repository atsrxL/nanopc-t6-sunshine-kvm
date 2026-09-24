// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "kvmhost.h"
#include <QWidget>
#include <QTimer>

class QLineEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;

class KvmWindow : public QWidget
{
    Q_OBJECT
    friend class ClientUiTest; // Offline lifecycle tests; these never contact a host.
public:
    KvmWindow();
    ~KvmWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void connectHost();
    void startStream();
    void settings();
    void pollDisplay();
    void runStream();
    void sessionEnded();
    void scheduleReconnect();
    void reconnectNow();
    void markConnected();
    void launchFailed(const QString& status, bool transport);
    void stopFollowing();
    void updateMouseModeEnabled();
    void updateStatus(const QString& status);

    KvmConfig m_config;
    KvmHost m_host;
    QLineEdit* m_address;
    QSpinBox* m_port;
    QLineEdit* m_password;
    QComboBox* m_mouseMode;
    QPushButton* m_connect;
    QPushButton* m_start;
    QLabel* m_status;
    QTimer m_poll;
    QTimer m_reconnectTimer;
    bool m_wantStream = false;
    bool m_restart = false;
    bool m_cleanup = false;
    bool m_reconnectPending = false;
    bool m_sessionConnected = false;
    int m_reconnectAttempt = 0;
    int m_pollFailures = 0;
    RkmoonDisplay m_pendingMode;
    int m_pendingModePolls = 0;
    QString m_sessionError;
    bool m_pollBusy = false;
    RkmoonDisplay m_runningMode;
    bool m_streaming = false;
    bool m_connecting = false;
};
