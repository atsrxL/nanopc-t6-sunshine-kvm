// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

class QNetworkRequest;
class QUrl;

// In-memory password credential for the single bound KVM host.
//
// The dedicated RK3588 server replaces GameStream PIN pairing with a password: every
// request to its six endpoints must carry
// "Authorization: Basic base64(UTF-8('kvm:' + password))" and is authorized per request.
// The user name is fixed; the operator supplies only an address, a port and a password.
//
// SECURITY BOUNDARY, deliberately chosen by the user: this scheme runs over plaintext
// HTTP on the server's base port. There is no TLS, no server certificate and no
// certificate pinning on the control channel, so anyone able to observe or modify that
// traffic can read the password and impersonate either side. Use it only on a network
// the operator already trusts. The GameStream RTSP/RTP control and input encryption that
// upstream performs on the streaming path is unchanged and still applies.
//
// Within that boundary the credential is still kept as narrow as possible: it exists only
// in this process's memory, is never written to QSettings, a URL, a query string, a file
// or a log line, and is attached only to requests whose scheme, host and port match the
// one bound target.
namespace RkmoonAuth
{
    // Fixed HTTP user name of the server's password scheme.
    extern const char* const USER_NAME;

    // Value the server publishes in the <RKMoonAuth> element of an authorized serverinfo.
    extern const char* const SCHEME_VERSION;

    // Stores the password in memory only. Nothing is ever sent until bindTarget() arms
    // the exact host and port it may be sent to.
    void setPassword(const QString& password);
    bool hasPassword();

    // Arms the stored password for exactly one http://host:port.
    void bindTarget(const QString& host, quint16 port);
    bool hasTarget();

    // Disarms the target but keeps the password in memory, so a failed or repeated
    // connection attempt cannot send anything while the operator does not have to retype
    // the password for the streaming session.
    void unbindTarget();

    // Best-effort wipe of the password and the target. Called when forgetting the host
    // binding, when rebinding another host and at shutdown.
    void clear();

    // True only for a URL whose scheme, host and port are the armed target.
    bool isBoundTarget(const QUrl& url);

    // The Authorization header value for the bound target, or an empty array for any
    // other URL, an unarmed credential or a missing password.
    QByteArray authorizationValue(const QUrl& url);

    // Disables automatic redirect following for every request, then adds the
    // Authorization header for the bound target only. Nothing else is changed.
    //
    // Redirects must stay manual: Qt follows them by default and replays the request
    // headers, so a 3xx answer from an impersonated host would hand the Authorization
    // header to whatever host, port or scheme the redirect names. Filtering the initial
    // URL alone cannot prevent that. This client's server never redirects.
    void setMouseMode(bool absolute);
    void prepareSessionUrl(QUrl& url, const QString& command);
    void prepareRequest(QNetworkRequest& request, const QUrl& url);
}
