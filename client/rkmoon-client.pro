# SPDX-License-Identifier: GPL-3.0-or-later
# RKMoon minimal KVM client. Builds only the upstream dependencies this client needs:
# moonlight-common-c (transport), h264bitstream (HEVC/H.264 bitstream fixups used by the
# FFmpeg decoder) and libsoundio (low latency audio output on Windows/macOS).
# qmdnsengine (mDNS host discovery) is deliberately not built: this client binds one host.
TEMPLATE = subdirs

isEmpty(MOONLIGHT_DIR): MOONLIGHT_DIR = $$PWD/../vendor/moonlight-qt
!exists($$MOONLIGHT_DIR/app/app.pro): error("MOONLIGHT_DIR does not contain the pinned moonlight-qt checkout: $$MOONLIGHT_DIR")

CONFIG += debug_and_release ordered

mcc.file = $$MOONLIGHT_DIR/moonlight-common-c/moonlight-common-c.pro
mcc.subdir = mcc
h264bs.file = $$MOONLIGHT_DIR/h264bitstream/h264bitstream.pro
h264bs.subdir = h264bs
rkmoonapp.file = $$PWD/app/rkmoon-app.pro
rkmoonapp.subdir = rkmoonapp

SUBDIRS = mcc h264bs
rkmoonapp.depends = mcc h264bs

win32|macx {
    soundio.file = $$MOONLIGHT_DIR/soundio/soundio.pro
    soundio.subdir = soundio
    SUBDIRS += soundio
    rkmoonapp.depends += soundio
}

win32:!winrt {
    antihooking.file = $$MOONLIGHT_DIR/AntiHooking/AntiHooking.pro
    antihooking.subdir = antihooking
    SUBDIRS += antihooking
    rkmoonapp.depends += antihooking
}

SUBDIRS += rkmoonapp
