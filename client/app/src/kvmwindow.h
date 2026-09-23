// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "kvmhost.h"
#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;

class KvmWindow : public QWidget
{
    Q_OBJECT
    friend class ClientUiTest; // Offline lifecycle tests; never contacts a host.
public:
    KvmWindow();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void connectHost();
    void startStream();
    void settings();
    void updateStatus(const QString& status);

    KvmConfig m_config;
    KvmHost m_host;
    QLineEdit* m_address;
    QPushButton* m_connect;
    QPushButton* m_start;
    QLabel* m_status;
    bool m_streaming = false;
    bool m_connecting = false;
};
