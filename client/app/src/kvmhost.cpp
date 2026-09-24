// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmhost.h"
#include "rkmoon_auth.h"

#include <stdexcept>
#include <QXmlStreamReader>

namespace {

constexpr int HTTP_TIMEOUT_MS = 5000;

// The server reports a rejected password the GameStream way: HTTP 200 carrying a root
// element with status_code=401, which NvHTTP::verifyResponseStatus turns into a
// GfeHttpResponseException. This transport-level check only covers a front end or proxy
// that answers with a real 401/403 status instead.
bool isAuthorizationFailure(const QtNetworkReplyException& e)
{
    return e.getError() == QNetworkReply::AuthenticationRequiredError ||
           e.getError() == QNetworkReply::ContentAccessDenied;
}

}

void KvmHost::setPassword(const QString& password)
{
    RkmoonAuth::setPassword(password);
}

void KvmHost::forget()
{
    // Forgetting drops the transport credential as well, so a later request cannot be
    // authorized with a password the operator believes they already stopped using.
    RkmoonAuth::clear();
    m_computer.reset();
    m_config.hostUuid.clear();
    m_config.hostName.clear();
    m_config.appId = 0;
    m_config.save();
}

void KvmHost::refresh()
{
    m_computer.reset();
    // Disarm before re-validating: nothing may be sent until this refresh has proved the
    // host and port again. The password itself stays in memory.
    RkmoonAuth::unbindTarget();

    if (m_config.address.isEmpty() || m_config.httpPort == 0) {
        throw std::runtime_error("Set a host address and port first");
    }
    if (!RkmoonAuth::hasPassword()) {
        throw std::runtime_error("Enter the host password first");
    }

    // From here on the password may travel, but only to this host and port.
    RkmoonAuth::bindTarget(m_config.address, m_config.httpPort);
    if (!RkmoonAuth::hasTarget()) {
        throw std::runtime_error("Incomplete host binding; check the address and port");
    }

    // One base port carries every endpoint. There is no certificate and no separate
    // HTTPS port, so serverinfo is requested directly instead of through
    // NvHTTP::getServerInfo(), whose transport selection and 401 fallback exist only for
    // the upstream two-port TLS layout.
    NvHTTP http(NvAddress(m_config.address, m_config.httpPort), 0, QSslCertificate());
    QString info;
    try {
        info = http.openConnectionToString(http.m_BaseUrlHttp,
                                           "serverinfo",
                                           nullptr,
                                           HTTP_TIMEOUT_MS,
                                           NvHTTP::NVLL_ERROR);
        NvHTTP::verifyResponseStatus(info);
    }
    catch (const GfeHttpResponseException& e) {
        RkmoonAuth::unbindTarget();
        if (e.getStatusCode() == 401 || e.getStatusCode() == 403) {
            throw std::runtime_error("Host rejected the password, or its failure budget is "
                                     "temporarily exhausted; wait a few seconds and retry");
        }
        throw;
    }
    catch (const QtNetworkReplyException& e) {
        RkmoonAuth::unbindTarget();
        if (isAuthorizationFailure(e)) {
            throw std::runtime_error("Host rejected the password");
        }
        throw;
    }

    acceptInfo(info);
}

void KvmHost::acceptInfo(const QString& info)
{
    NvHTTP http(NvAddress(m_config.address, m_config.httpPort), 0, QSslCertificate());
    // Only an authorized answer carries the scheme marker, so an unauthenticated or
    // foreign server cannot be mistaken for a bound host.
    if (NvHTTP::getXmlString(info, "RKMoonAuth") != QLatin1String(RkmoonAuth::SCHEME_VERSION)) {
        RkmoonAuth::unbindTarget();
        throw std::runtime_error("This host does not speak RKMoon password authentication");
    }

    auto mode = RkmoonDisplay::parse(info);
    auto host = std::make_unique<NvComputer>(http, info);
    if (host->uuid.isEmpty() || host->appVersion.isEmpty()) {
        RkmoonAuth::unbindTarget();
        throw std::runtime_error("Incomplete GameStream server information");
    }
    if (!m_config.hostUuid.isEmpty() && host->uuid != m_config.hostUuid) {
        RkmoonAuth::unbindTarget();
        throw std::runtime_error("Host identity changed; forget the binding and bind again");
    }
    // The server reports an authorized session as PairStatus=1 for upstream compatibility.
    if (host->pairState != NvComputer::PS_PAIRED) {
        RkmoonAuth::unbindTarget();
        throw std::runtime_error("Host did not accept this client's password");
    }

    if (m_config.hostUuid.isEmpty()) {
        // A newly bound host never inherits a previous host's resolved app id.
        m_config.appId = 0;
    }
    m_config.hostUuid = host->uuid;
    m_config.hostName = host->name;
    m_config.save();
    const auto advertisedModes = NvHTTP::getXmlString(info, "RKMoonMouseModes");
    const auto mouseModes = advertisedModes.split(',');
    supportsAbsoluteMouse = mouseModes.contains("absolute");
    // Pre-capability RKMoon hosts implemented relative HID only.
    supportsRelativeMouse = advertisedModes.isEmpty() || mouseModes.contains("relative");
    display = mode;
    if (!streaming) m_computer = std::move(host);
}

NvApp KvmHost::hdmiApp()
{
    if (!m_computer || m_computer->pairState != NvComputer::PS_PAIRED ||
        !RkmoonAuth::hasTarget() || !RkmoonAuth::hasPassword()) {
        throw std::runtime_error("Connect to the bound host with its password before streaming");
    }
    NvHTTP http(m_computer.get());
    const auto apps = http.getAppList(); // authorized upstream request
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
    throw std::runtime_error("Configured HDMI app not found on this host");
}

void KvmHost::refreshAsync(std::function<void(QString)> done, bool apps)
{
    QUrl url;
    url.setScheme("http"); url.setHost(m_config.address); url.setPort(m_config.httpPort);
    url.setPath(apps ? "/applist" : "/serverinfo");
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);
    RkmoonAuth::prepareRequest(request, url);
    auto* reply = m_network.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &m_network, [this, reply, done, apps] {
        const auto body = QString::fromUtf8(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        transientFailure = status == 0 &&
            (reply->error() == QNetworkReply::TimeoutError ||
             reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
             reply->error() == QNetworkReply::RemoteHostClosedError ||
             reply->error() == QNetworkReply::ConnectionRefusedError);
        const bool failed = reply->error() != QNetworkReply::NoError ||
                            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200;
        reply->deleteLater();
        try {
            if (failed) throw std::runtime_error("Host request failed; check connection and password");
            NvHTTP::verifyResponseStatus(body);
            if (!apps) acceptInfo(body);
            else {
                QXmlStreamReader xml(body);
                NvApp app;
                bool found = false;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement()) {
                        if (xml.name() == QLatin1String("App")) app = NvApp();
                        else if (xml.name() == QLatin1String("AppTitle")) app.name = xml.readElementText();
                        else if (xml.name() == QLatin1String("ID")) app.id = xml.readElementText().toInt();
                    }
                    if (xml.isEndElement() && xml.name() == QLatin1String("App") &&
                        app.name == m_config.appName && app.id > 0 &&
                        (!m_config.appId || m_config.appId == app.id)) {
                        selectedApp = app; found = true;
                    }
                }
                if (xml.hasError() || !found) throw std::runtime_error("Configured HDMI app not found");
                if (m_computer->currentGameId && m_computer->currentGameId != selectedApp.id)
                    throw std::runtime_error("Host is busy with another application");
                m_config.appId = selectedApp.id; m_config.save();
            }
            done(QString());
        }
        catch (const GfeHttpResponseException& e) { done(e.toQString()); }
        catch (const std::exception& e) { done(QString::fromUtf8(e.what())); }
    });
}
