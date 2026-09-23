// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmhost.h"
#include "backend/nvpairingmanager.h"

#include <stdexcept>

void KvmHost::refresh()
{
    m_computer.reset();
    if (m_config.address.isEmpty() || m_config.httpPort == 0) {
        throw std::runtime_error("Set a host address first");
    }
    QSslCertificate saved(m_config.serverCertPem);
    if (!m_config.serverCertPem.isEmpty() && saved.isNull()) {
        throw std::runtime_error("Invalid saved host certificate; rebind explicitly");
    }
    NvAddress address(m_config.address, m_config.httpPort);
    // Discover the HTTPS port via upstream serverinfo, then authenticate with
    // the saved certificate. Supports non-default base ports without guessing.
    NvHTTP http(address, 0, saved);
    QString info = http.getServerInfo(NvHTTP::NVLL_ERROR);
    auto host = std::make_unique<NvComputer>(http, info);
    if (host->uuid.isEmpty() || host->appVersion.isEmpty()) {
        throw std::runtime_error("Incomplete GameStream server information");
    }
    if (!m_config.hostUuid.isEmpty() && host->uuid != m_config.hostUuid) {
        throw std::runtime_error("Host identity changed; rebind explicitly");
    }
    // A saved cert must never be replaced by a fresh unauthenticated serverinfo.
    if (!saved.isNull() && host->pairState != NvComputer::PS_PAIRED) {
        throw std::runtime_error("Host is no longer paired; rebind explicitly");
    }
    if (!saved.isNull() && host->serverCert != saved) {
        throw std::runtime_error("Host certificate changed; rebind explicitly");
    }
    m_computer = std::move(host);
}

void KvmHost::pair(const QString& pin)
{
    if (!m_computer || !m_config.serverCertPem.isEmpty() || pin.size() != 4) {
        throw std::runtime_error("Pairing requires a new host and a four-digit PIN");
    }
    for (QChar c : pin) {
        if (!c.isDigit() || c.unicode() > '9') {
            throw std::runtime_error("PIN must contain four digits");
        }
    }
    NvPairingManager pairing(m_computer.get());
    QSslCertificate cert;
    switch (pairing.pair(m_computer->appVersion, pin, cert)) {
    case NvPairingManager::PAIRED: break;
    case NvPairingManager::PIN_WRONG: throw std::runtime_error("Incorrect pairing PIN");
    default: throw std::runtime_error("Pairing failed or already in progress");
    }
    if (cert.isNull()) {
        throw std::runtime_error("Pairing returned no server certificate");
    }
    m_computer->serverCert = cert;
    NvHTTP http(m_computer.get());
    auto verified = std::make_unique<NvComputer>(http, http.getServerInfo(NvHTTP::NVLL_ERROR));
    if (verified->uuid != m_computer->uuid || verified->pairState != NvComputer::PS_PAIRED || verified->serverCert != cert) {
        throw std::runtime_error("Pairing identity verification failed");
    }
    m_config.hostUuid = verified->uuid;
    m_config.hostName = verified->name;
    m_config.serverCertPem = cert.toPem();
    m_config.appId = 0;
    m_config.save();
    m_computer = std::move(verified);
}

NvApp KvmHost::hdmiApp()
{
    if (!m_computer || m_computer->pairState != NvComputer::PS_PAIRED ||
        m_config.serverCertPem.isEmpty() || m_computer->serverCert.toPem() != m_config.serverCertPem) {
        throw std::runtime_error("Pair with the bound host before streaming");
    }
    NvHTTP http(m_computer.get());
    const auto apps = http.getAppList(); // authenticated upstream HTTPS request
    for (const auto& app : apps) {
        if (app.name == m_config.appName && (m_config.appId == 0 || m_config.appId == app.id)) {
            if (m_computer->currentGameId != 0 && m_computer->currentGameId != app.id) {
                throw std::runtime_error("Host is busy with a different session");
            }
            m_config.appId = app.id;
            m_config.save();
            return app;
        }
    }
    throw std::runtime_error("Configured HDMI app not found on paired host");
}
