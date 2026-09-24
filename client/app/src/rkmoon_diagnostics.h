// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
// Only explicit lifecycle labels and numeric facts. Never pass server text,
// URLs, queries, password/authorization, key material or input events here.
namespace RkmoonDiagnostics {
inline QString path() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/client-lifecycle.log";
}
inline void record(const QString& event) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const auto filePath = path();
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (file.size() > 512 * 1024) {
        QFile::remove(filePath + ".previous");
        file.rename(filePath + ".previous");
        file.setFileName(filePath);
    }
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " " + event + "\n").toUtf8());
}
}
