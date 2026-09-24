// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_auth.h"

#include <QMutex>
#include <QMutexLocker>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace RkmoonAuth
{

const char* const USER_NAME = "kvm";
const char* const SCHEME_VERSION = "password-http-v1";

namespace
{
// The upstream Session and its deferred cleanup task build NvHTTP instances on worker
// threads, so every access is serialized.
QMutex g_Lock;
QString g_Password;
QString g_Host;
quint16 g_Port = 0;
bool g_AbsoluteMouse = true;

// Overwrites the characters of the only copy this process keeps before releasing them.
// This cannot undo a copy Qt already made elsewhere; it only shortens the window in
// which the password sits in a freed allocation.
void wipe(QString& secret)
{
    secret.fill(QChar(u'\0'));
    secret.clear();
}

bool matchesTargetLocked(const QUrl& url)
{
    if (g_Host.isEmpty() || g_Port == 0) {
        return false;
    }
    // This server speaks plaintext HTTP only. Anything else is not the bound target, so
    // the credential is withheld rather than sent over an unexpected scheme.
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (url.port(0) != g_Port) {
        return false;
    }
    // Host names are compared case-insensitively; addresses are compared literally,
    // exactly as they were bound. No DNS resolution or normalization is attempted,
    // because a mismatch here must fail closed rather than widen the target.
    return url.host().compare(g_Host, Qt::CaseInsensitive) == 0;
}

QByteArray authorizationValueLocked()
{
    if (g_Password.isEmpty()) {
        return QByteArray();
    }
    QByteArray credential = QByteArray(USER_NAME) + ':' + g_Password.toUtf8();
    QByteArray value = QByteArray("Basic ") + credential.toBase64();
    credential.fill('\0');
    return value;
}

}

void setPassword(const QString& password)
{
    QMutexLocker locker(&g_Lock);
    wipe(g_Password);
    g_Password = password;
}

bool hasPassword()
{
    QMutexLocker locker(&g_Lock);
    return !g_Password.isEmpty();
}

void bindTarget(const QString& host, quint16 port)
{
    QMutexLocker locker(&g_Lock);
    if (host.isEmpty() || port == 0) {
        // Refuse to arm an incomplete target instead of guessing a default port.
        g_Host.clear();
        g_Port = 0;
        return;
    }
    g_Host = host;
    g_Port = port;
}

bool hasTarget()
{
    QMutexLocker locker(&g_Lock);
    return !g_Host.isEmpty() && g_Port != 0;
}

void unbindTarget()
{
    QMutexLocker locker(&g_Lock);
    g_Host.clear();
    g_Port = 0;
}

void clear()
{
    QMutexLocker locker(&g_Lock);
    wipe(g_Password);
    g_Host.clear();
    g_Port = 0;
}

bool isBoundTarget(const QUrl& url)
{
    QMutexLocker locker(&g_Lock);
    return matchesTargetLocked(url);
}

QByteArray authorizationValue(const QUrl& url)
{
    QMutexLocker locker(&g_Lock);
    if (!matchesTargetLocked(url)) {
        return QByteArray();
    }
    return authorizationValueLocked();
}

void setMouseMode(bool absolute)
{
    QMutexLocker lock(&g_Lock);
    g_AbsoluteMouse = absolute;
}

void prepareSessionUrl(QUrl& url, const QString& command)
{
    QMutexLocker lock(&g_Lock);
    if (!matchesTargetLocked(url) || (command != "launch" && command != "resume")) return;
    QUrlQuery query(url);
    query.removeAllQueryItems("rkmoonMouseMode");
    query.addQueryItem("rkmoonMouseMode", g_AbsoluteMouse ? "absolute" : "relative");
    url.setQuery(query);
}

void prepareRequest(QNetworkRequest& request, const QUrl& url)
{
    QMutexLocker locker(&g_Lock);

    // Applies to every request, credential or not: Qt follows redirects by default and
    // resends the request headers to the redirect target, which would move an
    // Authorization header to another host, port or scheme behind this filter's back.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    if (!matchesTargetLocked(url)) {
        return;
    }
    const QByteArray value = authorizationValueLocked();
    if (!value.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"), value);
    }
}

}
