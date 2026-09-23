// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmconfig.h"

#include "settings/streamingpreferences.h"

#include <QSettings>

#define SER_ADDRESS "host/address"
#define SER_HTTPPORT "host/httpPort"
#define SER_NAME "host/name"
#define SER_UUID "host/uuid"
#define SER_CERT "host/serverCert"
#define SER_APPID "host/appId"
#define SER_APPNAME "host/appName"
#define SER_WIDTH "video/width"
#define SER_HEIGHT "video/height"
#define SER_FPS "video/fps"
#define SER_BITRATE "video/bitrateKbps"
#define SER_CODEC "video/codec"
#define SER_FULLSCREEN "video/fullScreen"
#define SER_VSYNC "video/vsync"
#define SER_FRAMEPACING "video/framePacing"
#define SER_CAPTURESYSKEYS "input/captureSystemKeys"
#define SER_QUITONDISCONNECT "host/quitAppOnDisconnect"
#define SER_VOLUME "audio/volumePercent"
#define SER_MUTED "audio/muted"

KvmConfig::KvmConfig()
{
    load();
}

void KvmConfig::load()
{
    QSettings settings;

    address = settings.value(SER_ADDRESS).toString();
    httpPort = static_cast<quint16>(qBound(1u, settings.value(SER_HTTPPORT, 47989).toUInt(), 65535u));
    hostName = settings.value(SER_NAME).toString();
    hostUuid = settings.value(SER_UUID).toString();
    serverCertPem = settings.value(SER_CERT).toByteArray();
    appId = settings.value(SER_APPID, 0).toInt();
    // The dedicated RK3588 server publishes exactly one app named "HDMI".
    appName = settings.value(SER_APPNAME, "HDMI").toString();

    width = qBound(640, settings.value(SER_WIDTH, 1920).toInt(), 3840);
    height = qBound(480, settings.value(SER_HEIGHT, 1080).toInt(), 2160);
    fps = qBound(24, settings.value(SER_FPS, 60).toInt(), 120);
    bitrateKbps = qBound(1000, settings.value(SER_BITRATE, 20000).toInt(), 80000);
    codec = static_cast<Codec>(qBound(0, settings.value(SER_CODEC, CODEC_AUTO).toInt(), 2));
    fullScreen = settings.value(SER_FULLSCREEN, true).toBool();
    vsync = settings.value(SER_VSYNC, true).toBool();
    framePacing = settings.value(SER_FRAMEPACING, false).toBool();
    captureSystemKeys = settings.value(SER_CAPTURESYSKEYS, true).toBool();
    quitAppOnDisconnect = settings.value(SER_QUITONDISCONNECT, true).toBool();

    volumePercent = qBound(0, settings.value(SER_VOLUME, 100).toInt(), 100);
    muted = settings.value(SER_MUTED, false).toBool();
}

void KvmConfig::save() const
{
    QSettings settings;

    settings.setValue(SER_ADDRESS, address);
    settings.setValue(SER_HTTPPORT, httpPort);
    settings.setValue(SER_NAME, hostName);
    settings.setValue(SER_UUID, hostUuid);
    settings.setValue(SER_CERT, serverCertPem);
    settings.setValue(SER_APPID, appId);
    settings.setValue(SER_APPNAME, appName);

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_CODEC, static_cast<int>(codec));
    settings.setValue(SER_FULLSCREEN, fullScreen);
    settings.setValue(SER_VSYNC, vsync);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CAPTURESYSKEYS, captureSystemKeys);
    settings.setValue(SER_QUITONDISCONNECT, quitAppOnDisconnect);

    settings.setValue(SER_VOLUME, volumePercent);
    settings.setValue(SER_MUTED, muted);
}

bool KvmConfig::isBound() const
{
    return !address.isEmpty();
}

bool KvmConfig::hasApp() const
{
    return appId != 0;
}

void KvmConfig::applyTo(StreamingPreferences& prefs) const
{
    prefs.width = width;
    prefs.height = height;
    prefs.fps = fps;
    prefs.bitrateKbps = bitrateKbps;
    prefs.unlockBitrate = true;
    prefs.packetSize = 0; // Preserve upstream path-MTU and LAN/VPN selection.

    switch (codec) {
    case CODEC_HEVC:
        prefs.videoCodecConfig = StreamingPreferences::VCC_FORCE_HEVC;
        break;
    case CODEC_H264:
        prefs.videoCodecConfig = StreamingPreferences::VCC_FORCE_H264;
        break;
    case CODEC_AUTO:
    default:
        prefs.videoCodecConfig = StreamingPreferences::VCC_AUTO;
        break;
    }

    // A KVM console is useless without a picture, so never fall back to software decoding
    // silently: require hardware decoding and fail loudly instead.
    prefs.videoDecoderSelection = StreamingPreferences::VDS_FORCE_HARDWARE;

    // The RK3588 encoder path is 8-bit 4:2:0 SDR only.
    prefs.enableHdr = false;
    prefs.enableYUV444 = false;

    prefs.windowMode = fullScreen ? prefs.recommendedFullScreenMode
                                  : StreamingPreferences::WM_WINDOWED;
    prefs.enableVsync = vsync;
    prefs.framePacing = framePacing;

    // Relative mouse mode matches the host's relative USB HID descriptor. Absolute mouse
    // mode would require the host to expose an absolute HID device.
    prefs.absoluteMouseMode = false;
    prefs.absoluteTouchMode = false;
    prefs.captureSysKeysMode = captureSystemKeys ? StreamingPreferences::CSK_FULLSCREEN
                                                 : StreamingPreferences::CSK_OFF;

    // Removed features: no gamepads, no host-side audio playback switch, no mDNS, no
    // Discord presence, no network blocking probe, no perf overlay by default.
    prefs.multiController = false;
    prefs.gamepadMouse = false;
    prefs.backgroundGamepad = false;
    prefs.swapFaceButtons = false;
    prefs.enableMdns = false;
    prefs.richPresence = false;
    prefs.detectNetworkBlocking = false;
    prefs.showPerformanceOverlay = false;
    prefs.gameOptimizations = false;

    // Stereo audio from the host's HDMI capture.
    prefs.audioConfig = StreamingPreferences::AC_STEREO;
    prefs.playAudioOnHost = false;
    prefs.muteOnFocusLoss = false;

    prefs.quitAppAfter = quitAppOnDisconnect;
    prefs.keepAwake = true;
    prefs.connectionWarnings = true;
}
