// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDebug>
#include <QString>

// Deliberately accepts only an operation name, never a QUrl or response body.
// Unknown or query-bearing names are discarded rather than redacted heuristically.
// The host password only ever travels in an Authorization header, which is never
// passed to this function and never logged anywhere in this client.
inline void rkmoonLogHttpRequest(const QString& command, bool timedOut = false)
{
    const bool known = command == "serverinfo" || command == "applist" ||
                       command == "appasset" || command == "launch" ||
                       command == "resume" || command == "cancel";
    const QString label = known ? command : QStringLiteral("request");
    if (timedOut) qWarning() << "HTTP operation timed out:" << label;
    else qInfo() << "HTTP operation:" << label;
}
