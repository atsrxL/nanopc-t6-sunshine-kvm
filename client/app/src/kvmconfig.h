// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

class StreamingPreferences;

// Persistent state of the single bound KVM host plus the fixed streaming policy.
//
// This client binds exactly one host, so there is no host list, no mDNS discovery and no
// game library. Everything lives in this client's own QSettings scope (organization
// "RKMoon"), which is separate from an official Moonlight installation's settings and
// pairing identity on the same machine.
class KvmConfig
{
public:
    enum Codec
    {
        CODEC_AUTO = 0,   // HEVC preferred, H.264 fallback (server codec support decides)
        CODEC_HEVC = 1,
        CODEC_H264 = 2,
    };

    KvmConfig();

    void load();
    void save() const;

    // True once an address has been stored. Pairing state is re-checked against the host.
    bool isBound() const;

    // True once the fixed app has been resolved on the host at least once.
    bool hasApp() const;

    void applyTo(StreamingPreferences& prefs) const;

    // Host binding
    QString address;
    quint16 httpPort;
    QString hostName;
    QString hostUuid;
    QByteArray serverCertPem;
    int appId;
    QString appName;

    // Fixed streaming policy
    int width;
    int height;
    int fps;
    int bitrateKbps;
    Codec codec;
    bool fullScreen;
    bool vsync;
    bool framePacing;
    bool captureSystemKeys;
    bool quitAppOnDisconnect;

    // Audio (the host keeps sending audio; this is client side playback only)
    int volumePercent;
    bool muted;
};
