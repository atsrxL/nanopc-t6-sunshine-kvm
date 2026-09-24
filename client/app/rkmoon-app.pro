# SPDX-License-Identifier: GPL-3.0-or-later
# RKMoon minimal KVM client application.
#
# Reused unmodified from the pinned moonlight-qt checkout: GameStream HTTP, the
# streaming Session, FFmpeg hardware decoding and renderers, Opus audio playback,
# and keyboard/mouse input.
#
# Not built at all: QML/Qt Quick UI, game library and app management, box art, mDNS
# discovery, multi-host ComputerManager, Discord rich presence, auto update checks,
# gamepad/joystick/haptics, CLI subcommands, NvPairingManager and IdentityManager (this
# server authenticates with a password over plaintext HTTP instead of GameStream PIN
# pairing, has no TLS listener and never requests a client certificate, so no client
# identity key or certificate is generated or stored).
QT += core gui widgets network xml
CONFIG += c++17
TARGET = rkmoon-client
TEMPLATE = app

isEmpty(MOONLIGHT_DIR): MOONLIGHT_DIR = $$PWD/../../vendor/moonlight-qt
ML = $$MOONLIGHT_DIR/app
!exists($$ML/streaming/session.cpp): error("MOONLIGHT_DIR does not contain the pinned moonlight-qt checkout: $$MOONLIGHT_DIR")

include($$MOONLIGHT_DIR/globaldefs.pri)

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000
DEFINES += RKMOON_MINIMAL_CLIENT
DEFINES += VERSION_STR=\\\"$$cat($$ML/version.txt)-rkmoon\\\"

INCLUDEPATH += $$PWD/src $$ML
INCLUDEPATH += $$MOONLIGHT_DIR/moonlight-common-c/moonlight-common-c/src
INCLUDEPATH += $$MOONLIGHT_DIR/h264bitstream/h264bitstream
DEPENDPATH += $$MOONLIGHT_DIR/moonlight-common-c/moonlight-common-c/src

win32 {
    contains(QT_ARCH, x86_64) {
        LIBS += -L$$MOONLIGHT_DIR/libs/windows/lib/x64
        INCLUDEPATH += $$MOONLIGHT_DIR/libs/windows/include/x64
    }
    contains(QT_ARCH, arm64) {
        LIBS += -L$$MOONLIGHT_DIR/libs/windows/lib/arm64
        INCLUDEPATH += $$MOONLIGHT_DIR/libs/windows/include/arm64
    }

    INCLUDEPATH += $$MOONLIGHT_DIR/libs/windows/include
    LIBS += ws2_32.lib winmm.lib dxva2.lib ole32.lib gdi32.lib user32.lib d3d9.lib dwmapi.lib dbghelp.lib
    LIBS += -llibssl -llibcrypto -lSDL2 -lSDL2_ttf -lavcodec -lavutil -lswscale -lopus -ldxgi -ld3d11

    # Work around a conflict with math.h inclusion between SDL and Qt 6
    DEFINES += _USE_MATH_DEFINES
    CONFIG += ffmpeg soundio
}

macx {
    INCLUDEPATH += $$MOONLIGHT_DIR/libs/mac/include
    INCLUDEPATH += $$MOONLIGHT_DIR/libs/mac/Frameworks/SDL2.framework/Versions/A/Headers
    INCLUDEPATH += $$MOONLIGHT_DIR/libs/mac/Frameworks/SDL2_ttf.framework/Versions/A/Headers
    LIBS += -L$$MOONLIGHT_DIR/libs/mac/lib -F$$MOONLIGHT_DIR/libs/mac/Frameworks

    # QMake doesn't handle framework-style includes correctly on its own
    QMAKE_CFLAGS += -F$$MOONLIGHT_DIR/libs/mac/Frameworks
    QMAKE_CXXFLAGS += -F$$MOONLIGHT_DIR/libs/mac/Frameworks
    QMAKE_OBJECTIVE_CFLAGS += -F$$MOONLIGHT_DIR/libs/mac/Frameworks

    LIBS += -lssl.3 -lcrypto.3 -lavcodec.61 -lavutil.59 -lswscale.8 -lopus -framework SDL2 -framework SDL2_ttf
    LIBS += -lobjc -framework VideoToolbox -framework AVFoundation -framework CoreVideo -framework CoreGraphics
    LIBS += -framework CoreMedia -framework AppKit -framework Metal -framework QuartzCore
    LIBS += -framework CoreAudio -framework AudioUnit

    CONFIG += ffmpeg soundio
}

unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += openssl sdl2 SDL2_ttf opus
    packagesExist(libavcodec) {
        PKGCONFIG += libavcodec libavutil libswscale
        CONFIG += ffmpeg
    }
    !disable-x11 {
        packagesExist(x11) {
            DEFINES += HAS_X11
            PKGCONFIG += x11
        }
    }
    !disable-wayland {
        packagesExist(wayland-client) {
            DEFINES += HAS_WAYLAND
            PKGCONFIG += wayland-client
        }
    }
}

# Upstream sources reused as-is (plus the audited overlay patches applied by
# client/tools/apply_moonlight.py).
SOURCES += \
    $$ML/backend/nvaddress.cpp \
    $$ML/backend/nvapp.cpp \
    $$ML/backend/nvcomputer.cpp \
    $$ML/backend/nvhttp.cpp \
    $$ML/path.cpp \
    $$ML/settings/streamingpreferences.cpp \
    $$ML/streaming/audio/audio.cpp \
    $$ML/streaming/audio/renderers/sdlaud.cpp \
    $$ML/streaming/input/abstouch.cpp \
    $$ML/streaming/input/input.cpp \
    $$ML/streaming/input/keyboard.cpp \
    $$ML/streaming/input/mouse.cpp \
    $$ML/streaming/input/reltouch.cpp \
    $$ML/streaming/session.cpp \
    $$ML/streaming/streamutils.cpp \
    $$ML/streaming/video/overlaymanager.cpp \
    $$ML/wm.cpp

HEADERS += \
    $$ML/backend/nvaddress.h \
    $$ML/backend/nvapp.h \
    $$ML/backend/nvcomputer.h \
    $$ML/backend/nvhttp.h \
    $$ML/path.h \
    $$ML/settings/streamingpreferences.h \
    $$ML/streaming/audio/renderers/renderer.h \
    $$ML/streaming/audio/renderers/sdl.h \
    $$ML/streaming/input/input.h \
    $$ML/streaming/session.h \
    $$ML/streaming/streamutils.h \
    $$ML/streaming/video/decoder.h \
    $$ML/streaming/video/overlaymanager.h \
    $$ML/utils.h

# RKMoon minimal client front end.
SOURCES += \
    $$PWD/src/kvmconfig.cpp \
    $$PWD/src/kvmhost.cpp \
    $$PWD/src/kvmwindow.cpp \
    $$PWD/src/nocompat.cpp \
    $$PWD/src/nogamepad.cpp \
    $$PWD/src/rkmoon_audio_control.cpp \
    $$PWD/src/rkmoon_audio_keys.cpp \
    $$PWD/src/rkmoon_auth.cpp

rkmoon_tests {
    QT += testlib
    TARGET = rkmoon-client-tests
    CONFIG += console
    SOURCES += ../tests/client_ui.cpp
} else {
    SOURCES += $$PWD/src/rkmoon_main.cpp
}

HEADERS += \
    $$PWD/src/kvmconfig.h \
    $$PWD/src/kvmhost.h \
    $$PWD/src/rkmoon_pointer.h \
    $$PWD/src/rkmoon_display.h \
    $$PWD/src/rkmoon_session_control.h \
    $$PWD/src/kvmwindow.h \
    $$PWD/src/rkmoon_audio_control.h \
    $$PWD/src/rkmoon_audio_keys.h \
    $$PWD/src/rkmoon_auth.h \
    $$PWD/src/rkmoon_diagnostics.h \
    $$PWD/src/rkmoon_http_log.h

RESOURCES += rkmoon.qrc

ffmpeg {
    message(FFmpeg decoder selected)

    DEFINES += HAVE_FFMPEG
    SOURCES += \
        $$ML/streaming/video/ffmpeg.cpp \
        $$ML/streaming/video/ffmpeg-renderers/genhwaccel.cpp \
        $$ML/streaming/video/ffmpeg-renderers/sdlvid.cpp \
        $$ML/streaming/video/ffmpeg-renderers/swframemapper.cpp \
        $$ML/streaming/video/ffmpeg-renderers/pacer/pacer.cpp

    HEADERS += \
        $$ML/streaming/video/ffmpeg.h \
        $$ML/streaming/video/ffmpeg-renderers/renderer.h \
        $$ML/streaming/video/ffmpeg-renderers/genhwaccel.h \
        $$ML/streaming/video/ffmpeg-renderers/sdlvid.h \
        $$ML/streaming/video/ffmpeg-renderers/swframemapper.h \
        $$ML/streaming/video/ffmpeg-renderers/pacer/pacer.h
}

win32 {
    HEADERS += $$ML/streaming/video/ffmpeg-renderers/dxutil.h
}

win32:!winrt {
    message(DXVA2 and D3D11VA renderers selected)

    SOURCES += \
        $$ML/streaming/video/ffmpeg-renderers/dxva2.cpp \
        $$ML/streaming/video/ffmpeg-renderers/d3d11va.cpp \
        $$ML/streaming/video/ffmpeg-renderers/pacer/dxvsyncsource.cpp

    HEADERS += \
        $$ML/streaming/video/ffmpeg-renderers/dxva2.h \
        $$ML/streaming/video/ffmpeg-renderers/d3d11va.h \
        $$ML/streaming/video/ffmpeg-renderers/pacer/dxvsyncsource.h
}

macx {
    message(VideoToolbox renderer selected)

    SOURCES += \
        $$ML/streaming/video/ffmpeg-renderers/vt_base.mm \
        $$ML/streaming/video/ffmpeg-renderers/vt_avsamplelayer.mm \
        $$ML/streaming/video/ffmpeg-renderers/vt_metal.mm

    HEADERS += $$ML/streaming/video/ffmpeg-renderers/vt.h
}

soundio {
    message(libsoundio audio renderer selected)

    DEFINES += HAVE_SOUNDIO SOUNDIO_STATIC_LIBRARY
    SOURCES += $$ML/streaming/audio/renderers/soundioaudiorenderer.cpp
    HEADERS += $$ML/streaming/audio/renderers/soundioaudiorenderer.h

    INCLUDEPATH += $$MOONLIGHT_DIR/soundio/libsoundio
    DEPENDPATH += $$MOONLIGHT_DIR/soundio/libsoundio
}

# Dependency libraries built by client/rkmoon-client.pro. DEPS_OUT can be overridden
# when the dependencies were built in a different shadow build directory.
# qmake's absolute subdir .file entries are shadow-built beside the build root.
isEmpty(DEPS_OUT): DEPS_OUT = $$OUT_PWD/../../vendor/moonlight-qt

win32:CONFIG(release, debug|release): LIBS += -L$$DEPS_OUT/moonlight-common-c/release/ -lmoonlight-common-c -L$$DEPS_OUT/h264bitstream/release/ -lh264bitstream
else:win32:CONFIG(debug, debug|release): LIBS += -L$$DEPS_OUT/moonlight-common-c/debug/ -lmoonlight-common-c -L$$DEPS_OUT/h264bitstream/debug/ -lh264bitstream
else:unix: LIBS += -L$$DEPS_OUT/moonlight-common-c/ -lmoonlight-common-c -L$$DEPS_OUT/h264bitstream/ -lh264bitstream

soundio {
    win32:CONFIG(release, debug|release): LIBS += -L$$DEPS_OUT/soundio/release/ -lsoundio
    else:win32:CONFIG(debug, debug|release): LIBS += -L$$DEPS_OUT/soundio/debug/ -lsoundio
    else:unix: LIBS += -L$$DEPS_OUT/soundio/ -lsoundio
}

win32:!winrt {
    win32:CONFIG(release, debug|release): LIBS += -L$$DEPS_OUT/AntiHooking/release/ -lAntiHooking
    else:win32:CONFIG(debug, debug|release): LIBS += -L$$DEPS_OUT/AntiHooking/debug/ -lAntiHooking

    INCLUDEPATH += $$MOONLIGHT_DIR/AntiHooking
    DEPENDPATH += $$MOONLIGHT_DIR/AntiHooking
}
