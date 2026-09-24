// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

class StreamingPreferences;

// Persistent state of the single bound KVM host plus the fixed streaming policy.
//
// This client binds exactly one host, so there is no host list, no mDNS discovery and no
// game library. Everything lives in this client's own QSettings scope (organization
// "RKMoon"), which is separate from an official Moonlight installation's settings and
// client identity on the same machine.
//
// The host password is deliberately NOT a member of this class: it is never persisted.
// See rkmoon_auth.h for the in-memory credential.
class KvmConfig
{
public:
    enum Codec
    {
        CODEC_AUTO = 0,   // HEVC preferred, H.264 fallback (server codec support decides)
        CODEC_HEVC = 1,
        CODEC_H264 = 2,
    };

    enum MouseMode { MOUSE_ABSOLUTE = 0, MOUSE_RELATIVE = 1 };
    MouseMode mouseMode = MOUSE_ABSOLUTE;

    KvmConfig();

    void load();
    void save() const;

    // True once an address has been stored. The password is re-checked by the host on
    // every request, so being bound never implies being authorized.
    bool isBound() const;

    // True once the fixed app has been resolved on the host at least once.
    bool hasApp() const;

    void applyTo(StreamingPreferences& prefs) const;

    // Host binding. The operator supplies the address and the port; everything else is
    // learned from the host's authorized answer. There is no certificate to pin: the
    // control channel is plaintext HTTP by design (see rkmoon_auth.h).
    QString address;
    quint16 httpPort;
    QString hostName;
    QString hostUuid;
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
