// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDebug>
#include <QString>

// Deliberately accepts only an operation name, never a QUrl or response body.
// Unknown or query-bearing names are discarded rather than redacted heuristically.
inline void rkmoonLogHttpRequest(const QString& command, bool timedOut = false)
{
    const bool known = command == "serverinfo" || command == "pair" ||
                       command == "applist" || command == "launch" ||
                       command == "resume" || command == "cancel" || command == "unpair";
    const QString label = known ? command : QStringLiteral("request");
    if (timedOut) qWarning() << "HTTP operation timed out:" << label;
    else qInfo() << "HTTP operation:" << label;
}
