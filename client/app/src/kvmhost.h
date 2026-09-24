// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "kvmconfig.h"
#include "backend/nvcomputer.h"

#include <memory>
#include <QNetworkAccessManager>
#include <functional>
#include "rkmoon_display.h"

// Single-host adapter.
//
// This server has no GameStream PIN pairing and no TLS on its control channel: it
// authorizes every request against a password over plaintext HTTP, on the one base port
// the operator supplies. See rkmoon_auth.h for the credential and its security boundary.
// Never launch or send input unless the host UUID and the password-authorized state both
// match this binding.
class KvmHost
{
public:
    explicit KvmHost(KvmConfig& config) : m_config(config) {}

    // Arms the in-memory password for the bound host. Nothing is transmitted here.
    void setPassword(const QString& password);

    // Drops the stored binding and the in-memory password.
    void forget();

    // Authenticates against the bound host. Throws on network, identity or password
    // failure.
    void refresh();

    void refreshAsync(std::function<void(QString)> done, bool apps = false);
    bool supportsAbsoluteMouse = false;
    bool supportsRelativeMouse = false;
    bool streaming = false;
    bool transientFailure = false; // Last async result; auth/protocol failures are never transient.
    RkmoonDisplay display;
    NvApp selectedApp;
    NvApp hdmiApp();                 // only the configured HDMI app; never launches another app
    NvComputer* computer() { return m_computer.get(); }

private:
    void acceptInfo(const QString& info);
    QNetworkAccessManager m_network;
    KvmConfig& m_config;
    std::unique_ptr<NvComputer> m_computer;
};
