#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Commit-locked, fail-closed moonlight-qt overlay for the minimal KVM client. Default operation is a dry run.

The overlay removes unshipped controller/QML/Discord/SVG features, disables absolute mouse
switching, redacts HTTP request/response logging, adds post-Opus mute/volume attenuation and
attaches the RKMoon password credential to requests that target the bound host, and points
every upstream request at the server's single plaintext HTTP base port. Video transport,
decoding and encrypted input packet implementations are retained. GameStream PIN pairing is
not patched out here: nvpairingmanager is simply not compiled by client/app/rkmoon-app.pro,
because this server has no /pair endpoint.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / 'sources.lock.json').read_text())['sources']['moonlight-qt']['commit']

FILES = [
    "app/settings/streamingpreferences.h",
    "app/streaming/input/input.h",
    "app/settings/streamingpreferences.cpp",
    "app/streaming/input/input.cpp",
    "app/streaming/input/keyboard.cpp",
    "app/streaming/input/mouse.cpp",
    "app/streaming/input/abstouch.cpp",
    "app/streaming/audio/audio.cpp",
    "app/streaming/session.cpp",
    "app/backend/nvhttp.cpp",
]


class PatchError(RuntimeError):
    pass


def once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise PatchError(f"anchor count {count}, expected 1: {old.splitlines()[0][:100]!r}")
    return text.replace(old, new, 1)


GAMEPAD_INIT_ANCHOR = """    // We must initialize joystick explicitly before gamecontroller in order
    // to ensure we receive gamecontroller attach events for gamepads where
    // SDL doesn't have a built-in mapping. By starting joystick first, we
    // can allow mapping manager to update the mappings before GC attach
    // events are generated.
    SDL_assert(!SDL_WasInit(SDL_INIT_JOYSTICK));
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_JOYSTICK) failed: %s",
                     SDL_GetError());
    }

    MappingManager mappingManager;
    mappingManager.applyMappings();

    // Flush gamepad arrival and departure events which may be queued before
    // starting the gamecontroller subsystem again. This prevents us from
    // receiving duplicate arrival and departure events for the same gamepad.
    SDL_FlushEvent(SDL_CONTROLLERDEVICEADDED);
    SDL_FlushEvent(SDL_CONTROLLERDEVICEREMOVED);

    // We need to reinit this each time, since you only get
    // an initial set of gamepad arrival events once per init.
    SDL_assert(!SDL_WasInit(SDL_INIT_GAMECONTROLLER));
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) failed: %s",
                     SDL_GetError());
    }
"""

GAMEPAD_INIT_REPLACEMENT = """    // RKMoon minimal KVM client: no joystick, gamecontroller or haptic subsystem is
    // initialized, no controller mappings are loaded and no controller packets are ever
    // sent to the host. See client/app/src/nogamepad.cpp for the no-op handlers.
"""

GAMEPAD_MASK_ANCHOR = """    // Initialize the gamepad mask with currently attached gamepads to avoid
    // causing gamepads to unexpectedly disappear and reappear on the host
    // during stream startup as we detect currently attached gamepads one at a time.
    m_GamepadMask = getAttachedGamepadMask();
"""

GAMEPAD_MASK_REPLACEMENT = """    // RKMoon minimal KVM client: the host is always told that zero gamepads are attached.
    m_GamepadMask = 0;
"""

GAMEPAD_QUIT_ANCHOR = """    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_assert(!SDL_WasInit(SDL_INIT_GAMECONTROLLER));

    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    SDL_assert(!SDL_WasInit(SDL_INIT_JOYSTICK));
"""

GAMEPAD_QUIT_REPLACEMENT = """    // RKMoon minimal KVM client: these subsystems were never initialized.
"""

PREFS_GET_ANCHOR = """StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(&s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(&s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}
"""

PREFS_GET_REPLACEMENT = """StreamingPreferences* StreamingPreferences::get()
{
    {
        QReadLocker readGuard(&s_GlobalPrefsLock);

        if (s_GlobalPrefs) {
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(&s_GlobalPrefsLock);

        // RKMoon minimal KVM client: there is no QML engine to associate with.
        if (!s_GlobalPrefs) {
            s_GlobalPrefs = new StreamingPreferences();
        }

        return s_GlobalPrefs;
    }
}
"""

PREFS_RETRANSLATE_HEAD_ANCHOR = """#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

"""

PREFS_RETRANSLATE_TAIL_ANCHOR = """    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

"""

PREFS_RETRANSLATE_TAIL_REPLACEMENT = """    // RKMoon minimal KVM client: there is no QML engine to refresh; the Qt Widgets
    // front end reads its own strings when it is constructed.

"""

AUDIO_GAIN_ANCHOR = """        // Update desiredSize with the number of bytes actually populated by the decoding operation
        if (samplesDecoded > 0) {
            SDL_assert(desiredBufferSize >= frameSize * samplesDecoded);
            desiredBufferSize = frameSize * samplesDecoded;
        }
"""

AUDIO_GAIN_REPLACEMENT = """        // Update desiredSize with the number of bytes actually populated by the decoding operation
        if (samplesDecoded > 0) {
            SDL_assert(desiredBufferSize >= frameSize * samplesDecoded);
            desiredBufferSize = frameSize * samplesDecoded;

            // RKMoon minimal KVM client: apply the client-side volume/mute gain. Muting is
            // implemented as a zero gain after decoding (rather than skipping the decode) so
            // the Opus decoder state and the renderer's timing stay continuous.
            RkmoonAudioControl::applyGain(buffer,
                                          samplesDecoded * s_ActiveSession->m_ActiveAudioConfig.channelCount,
                                          s_ActiveSession->m_AudioRenderer->getAudioBufferFormat() == IAudioRenderer::AudioFormat::Float32NE);
        }
"""


HTTP_AUTH_ANCHOR = """    QNetworkRequest request(url);

    // Add our client certificate
    request.setSslConfiguration(IdentityManager::get()->getSslConfig());
"""

HTTP_AUTH_REPLACEMENT = """    RkmoonAuth::prepareSessionUrl(url, command);
    QNetworkRequest request(url);

    // RKMoon minimal KVM client: the upstream client certificate is deliberately not
    // installed. This server has no TLS listener and never asks for a client
    // certificate, so IdentityManager is not compiled into this client at all and no
    // RSA identity key or certificate is generated or stored on this machine.
    //
    // Instead, this server authorizes every request with a password. The credential is
    // added only for the one bound http://host:port. Every request the client makes,
    // including the streaming session's launch/resume/cancel, goes through this
    // function. The password is never part of the URL, so it cannot reach a log, and it
    // is never attached to another host, port or scheme. This call also forces manual
    // redirect handling for every request.
    RkmoonAuth::prepareRequest(request, url);
"""


# This server exposes all six endpoints on one plaintext HTTP base port and reports
# HttpsPort=0. Upstream would otherwise send applist/launch/resume/cancel/appasset to
# https://host:47984, and NvComputer would substitute the default HTTPS port for the
# zero the server reports. Redirecting the "HTTPS" base URL onto the same HTTP base port
# keeps every upstream call site unchanged while speaking the server's actual protocol.
HTTPS_SCHEME_ANCHOR = """    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");
"""

HTTPS_SCHEME_REPLACEMENT = """    m_BaseUrlHttp.setScheme("http");
    // RKMoon minimal KVM client: the dedicated server has no TLS listener. The upstream
    // "HTTPS" base URL is deliberately the same plaintext HTTP origin as the base URL.
    m_BaseUrlHttps.setScheme("http");
"""

HTTPS_ADDRESS_ANCHOR = """    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
}
"""

HTTPS_ADDRESS_REPLACEMENT = """    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
    // RKMoon minimal KVM client: one base port serves every endpoint.
    m_BaseUrlHttps.setPort(address.port());
}
"""

HTTPS_PORT_ANCHOR = """void NvHTTP::setHttpsPort(uint16_t port)
{
    m_BaseUrlHttps.setPort(port);
}
"""

HTTPS_PORT_REPLACEMENT = """void NvHTTP::setHttpsPort(uint16_t port)
{
    // RKMoon minimal KVM client: the server reports HttpsPort=0 and listens on nothing
    // else, so a separate TLS port is never adopted. Callers that pass the upstream
    // default, or the value NvComputer substitutes for zero, keep using the base port.
    Q_UNUSED(port);
    m_BaseUrlHttps.setPort(m_BaseUrlHttp.port());
}
"""


CURSOR_ANCHOR = '        if (!SDL_GetRelativeMouseMode()) {\n            m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;\n            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);\n        }\n        else {\n            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,\n                        "Cursor can only be shown in remote desktop mouse mode");\n        }\n'
CURSOR_REPLACEMENT = '        // Toggle local visibility only; preserve absolute/relative protocol mode.\n        setCaptureActive(false);\n        m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;\n        setCaptureActive(true);\n'
CAPTURE_ANCHOR = "if (m_AbsoluteMouseMode || SDL_SetRelativeMouseMode(SDL_TRUE) < 0) {"
PUMP_ANCHOR = "    SDL_Event event;\n    for (;;) {\n"

TERMINATE_ANCHOR = '    SDL_Event event;\n    event.type = SDL_QUIT;\n    event.quit.timestamp = SDL_GetTicks();\n    SDL_PushEvent(&event);\n'

POINTER_CLAMP_ANCHOR = '        // Clamp motion to the video region\n        x = qMin(qMax(x - dst.x, 0), dst.w);\n        y = qMin(qMax(y - dst.y, 0), dst.h);\n'
POINTER_TOUCH_ANCHOR = '        short x = qMin(qMax((int)(event->x * windowWidth), dst.x), dst.x + dst.w);\n        short y = qMin(qMax((int)(event->y * windowHeight), dst.y), dst.y + dst.h);\n\n        // Update the cursor position relative to the video region\n        LiSendMousePositionEvent(x - dst.x, y - dst.y, dst.w, dst.h);'
POINTER_INSIDE_ANCHOR = '    return (mouseX >= dst.x && mouseX <= dst.x + dst.w) &&\n           (mouseY >= dst.y && mouseY <= dst.y + dst.h);'

def make_changes(original):
    changes = {}
    for path, text in original.items():
        if path == "app/settings/streamingpreferences.h":
            text = once(text, "#include <QQmlEngine>\n",
                        "// RKMoon minimal KVM client: the QML engine binding is removed (no QtQml dependency).\n")
            text = once(text, "    static StreamingPreferences* get(QQmlEngine *qmlEngine = nullptr);",
                        "    static StreamingPreferences* get();")
            text = once(text, "    explicit StreamingPreferences(QQmlEngine *qmlEngine);",
                        "    explicit StreamingPreferences();")
            text = once(text, "\n    QQmlEngine* m_QmlEngine;\n", "\n")
        elif path == "app/settings/streamingpreferences.cpp":
            text = once(text, "StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine)\n    : m_QmlEngine(qmlEngine)\n{",
                        "StreamingPreferences::StreamingPreferences()\n{")
            text = once(text, PREFS_GET_ANCHOR, PREFS_GET_REPLACEMENT)
            text = once(text, PREFS_RETRANSLATE_HEAD_ANCHOR, "")
            text = once(text, PREFS_RETRANSLATE_TAIL_ANCHOR, PREFS_RETRANSLATE_TAIL_REPLACEMENT)
        elif path == "app/streaming/input/input.h":
            text = once(text, '    bool m_AbsoluteTouchMode;', '    bool m_AbsoluteTouchMode;\n    QSet<SDL_FingerID> m_RkmoonAcceptedTouches;')
            text = once(text, '#pragma once', '#pragma once\n#include <QSet>')
            # ComputerManager pulls in qmdnsengine (mDNS discovery), which this client does not use.
            text = once(text, '#include "backend/computermanager.h"',
                        '#include "backend/nvcomputer.h" // RKMoon: was backend/computermanager.h (no mDNS discovery)')
        elif path == "app/streaming/input/input.cpp":
            text = once(text, "m_MouseCursorCapturedVisibilityState(SDL_DISABLE)", "m_MouseCursorCapturedVisibilityState(prefs.absoluteMouseMode ? SDL_ENABLE : SDL_DISABLE)")
            text = once(text, CAPTURE_ANCHOR, "if (m_AbsoluteMouseMode || m_MouseCursorCapturedVisibilityState == SDL_ENABLE || SDL_SetRelativeMouseMode(SDL_TRUE) < 0) {")
            text = once(text, GAMEPAD_INIT_ANCHOR, GAMEPAD_INIT_REPLACEMENT)
            text = once(text, GAMEPAD_MASK_ANCHOR, GAMEPAD_MASK_REPLACEMENT)
            text = once(text, GAMEPAD_QUIT_ANCHOR, GAMEPAD_QUIT_REPLACEMENT)
            text = once(text, '    m_SpecialKeyCombos[KeyComboToggleMouseMode].enabled = true;',
                        '    m_SpecialKeyCombos[KeyComboToggleMouseMode].enabled = false; // Relative HID only')
        elif path == "app/streaming/input/mouse.cpp":
            text = once(text, '#include "input.h"\n', '#include "input.h"\n#include "rkmoon_pointer.h"\n')
            text = once(text, POINTER_CLAMP_ANCHOR, '        const auto position = RkmoonPointer::position(x, y, dst);\n        x = position.x;\n        y = position.y;\n')
            text = once(text, POINTER_INSIDE_ANCHOR, '    return RkmoonPointer::inside(mouseX, mouseY, dst);')
        elif path == "app/streaming/input/abstouch.cpp":
            text = once(text, '#include "input.h"\n', '#include "input.h"\n#include "rkmoon_pointer.h"\n')
            text = once(text, 'if (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) {', 'if (false) { // Dedicated RKMoon host: always emulate absolute mouse, never native touch')
            text = once(text, POINTER_TOUCH_ANCHOR, '        const auto point = RkmoonPointer::position(event->x * windowWidth, event->y * windowHeight, dst);\n        LiSendMousePositionEvent(point.x, point.y, dst.w, dst.h);')
            text = once(text, '    // Scale window-relative events to be video-relative and clamp to video region\n    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);', '    // Scale window-relative events to be video-relative and clamp to video region\n    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);\n    if (event->type == SDL_FINGERDOWN) {\n        if (!RkmoonPointer::inside(event->x * windowWidth, event->y * windowHeight, dst)) return;\n        m_RkmoonAcceptedTouches.insert(event->fingerId);\n    } else if (!m_RkmoonAcceptedTouches.contains(event->fingerId)) return;\n    if (event->type == SDL_FINGERUP) m_RkmoonAcceptedTouches.remove(event->fingerId);')
        elif path == "app/streaming/input/keyboard.cpp":
            text = once(text, CURSOR_ANCHOR, CURSOR_REPLACEMENT)
        elif path == "app/streaming/audio/audio.cpp":
            text = once(text, '#include "renderers/sdl.h"\n',
                        '#include "renderers/sdl.h"\n#include "rkmoon_audio_control.h"\n')
            text = once(text, AUDIO_GAIN_ANCHOR, AUDIO_GAIN_REPLACEMENT)
        elif path == "app/streaming/session.cpp":
            text = once(text, PUMP_ANCHOR, PUMP_ANCHOR + "        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);\n        if (RkmoonSessionControl::internalQuitPending.exchange(false)) goto DispatchDeferredCleanup;\n")
            text = once(text, "SDL_WaitEventTimeout(&event, 1000)", "SDL_WaitEventTimeout(&event, 20)")
            text = once(text, '#include <QCursor>\n', '#include <QCursor>\n#include "rkmoon_session_control.h"\n#include "rkmoon_diagnostics.h"\n')
            text = once(text, TERMINATE_ANCHOR, '    RkmoonSessionControl::stop();\n')
            text = once(text, 'void Session::clConnectionTerminated(int errorCode)\n{', 'void Session::clConnectionTerminated(int errorCode)\n{\n    RkmoonDiagnostics::record(QString("connection-terminated code=%1").arg(errorCode));')
            text = once(text, 'void Session::clStageStarting(int stage)\n{', 'void Session::clStageStarting(int stage)\n{\n    RkmoonDiagnostics::record(QString("stage-start stage=%1").arg(stage));')
            text = once(text, 'void Session::clStageFailed(int stage, int errorCode)\n{', 'void Session::clStageFailed(int stage, int errorCode)\n{\n    RkmoonDiagnostics::record(QString("stage-failed stage=%1 code=%2").arg(stage).arg(errorCode));')
            text = once(text, '        case SDL_QUIT:\n', '        case SDL_QUIT:\n            RkmoonDiagnostics::record("sdl-quit-dispatched");\n')

            # Upstream SVG window icon is absent from this client; don't require QtSvg.
            text = once(text, '#include "backend/richpresencemanager.h"\n', '')
            text = once(text, '#include <QSvgRenderer>\n', '')
            text = once(text, '#include <QPainter>\n', '')
            text = once(text, '    RichPresenceManager presence(*m_Preferences, m_App.name);\n', '')
            if text.count('            presence.runCallbacks();\n') != 6:
                raise PatchError('unexpected rich presence callback count')
            text = text.replace('            presence.runCallbacks();\n', '')
            start = text.index('    QSvgRenderer svgIconRenderer(QString(":/res/moonlight.svg"));')
            end = text.index('    // Update the window display mode', start)
            if hashlib.sha256(text[start:end].encode()).hexdigest() != '81bb5a75e95fa7c3f4c90a64de96a65303753e9eeb5e8eb9741d433e7778e60b':
                raise PatchError('SVG icon block differs from audited pin')
            text = once(text, text[start:end], '    // RKMoon: no SVG resource or QtSvg icon dependency.\n\n')
            text = once(text, '    if (iconSurface != nullptr) {\n        SDL_FreeSurface(iconSurface);\n    }\n', '')
        elif path == "app/backend/nvhttp.cpp":
            # Launch URLs contain rikey and pairing requests contain authentication
            # material. Never log URL query strings or launch response bodies.
            text = once(text, '#include "nvcomputer.h"\n',
                        '#include "nvcomputer.h"\n#include "rkmoon_auth.h"\n#include "rkmoon_http_log.h"\n')
            text = once(text, HTTP_AUTH_ANCHOR, HTTP_AUTH_REPLACEMENT)
            text = once(text, HTTPS_SCHEME_ANCHOR, HTTPS_SCHEME_REPLACEMENT)
            text = once(text, HTTPS_ADDRESS_ANCHOR, HTTPS_ADDRESS_REPLACEMENT)
            text = once(text, HTTPS_PORT_ANCHOR, HTTPS_PORT_REPLACEMENT)
            text = once(text, 'qInfo() << "Executing request:" << url.toString();',
                        'rkmoonLogHttpRequest(command);')
            text = once(text, 'qWarning() << "Aborting timed out request for" << url.toString();',
                        'rkmoonLogHttpRequest(command, true);')
            text = once(text, 'qInfo() << "Launch response:" << response;',
                        'qInfo() << "Launch response received";')
            text = once(text, 'qInfo() << "Quit response:" << response;',
                        'qInfo() << "Quit response received";')
        else:
            raise PatchError("unexpected source file")
        changes[path] = text
    return changes


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).rstrip('\n')


COMMON_PATH = "moonlight-common-c/moonlight-common-c"
COMMON_FILE = COMMON_PATH + "/src/Connection.c"
COMMON_PIN = "8599b6042a4ba27749b0f94134dd614b4328a9bc"
COMMON_WAKE = """    // Wiggle the mouse a bit to wake the display up
    LiSendMouseMoveEvent(1, 1);
    PltSleepMs(10);
    LiSendMouseMoveEvent(-1, -1);
    PltSleepMs(10);
"""

def common_change(repo):
    nested = repo / COMMON_PATH
    if git(nested, "rev-parse", "HEAD") != COMMON_PIN:
        raise PatchError("moonlight-common-c HEAD does not match audited pin")
    original = subprocess.check_output(['git', '-C', str(nested), 'show', 'HEAD:src/Connection.c'], text=True)
    changed = once(original, COMMON_WAKE,
                   "    // RKMoon HDMI: no synthetic relative motion on connection.\n")
    for line in git(nested, "status", "--porcelain").splitlines():
        if line[3:] != "src/Connection.c":
            raise PatchError("unrelated moonlight-common-c edits")
    if (repo / COMMON_FILE).read_text() not in (original, changed):
        raise PatchError("unknown Connection.c changes")
    return original, changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path, help="independent pinned moonlight-qt checkout")
    parser.add_argument("--apply", action="store_true",
                        help="explicitly modify only this independent pinned checkout")
    parser.add_argument("--patch-output", type=Path, help="optional generated unified diff path")
    args = parser.parse_args()
    repo = args.repository.resolve()
    if git(repo, "rev-parse", "HEAD") != PIN:
        raise PatchError("moonlight-qt HEAD does not match the audited pin")
    original = {p: subprocess.check_output(['git', '-C', str(repo), 'show', 'HEAD:' + p], text=True)
                for p in FILES}
    changes = make_changes(original)
    common_original, common_patched = common_change(repo)
    original[COMMON_FILE] = common_original
    changes[COMMON_FILE] = common_patched
    paths = [*FILES, COMMON_FILE]
    dirty = git(repo, "status", "--porcelain").splitlines()
    for line in dirty:
        path = line[3:]
        if path == COMMON_PATH:
            continue # nested pin and every dirty file checked by common_change
        if path not in FILES or (repo / path).read_text() not in (original[path], changes[path]):
            raise PatchError("checkout contains unrelated edits; refuse to overwrite local work")
    if args.apply and any((repo / p).read_text() != original[p] for p in paths):
        raise PatchError("apply requires all original files; use a fresh pinned checkout")
    diff = "".join("".join(difflib.unified_diff(original[p].splitlines(True),
                                                changes[p].splitlines(True),
                                                fromfile="a/" + p, tofile="b/" + p)) for p in paths)
    if args.patch_output:
        args.patch_output.write_text(diff)
    if args.apply:
        try:
            for p, s in changes.items():
                tmp = repo / (p + ".rkmoon-new")
                tmp.write_text(s)
                os.replace(tmp, repo / p)
        except BaseException:
            for p, s in original.items():
                (repo / p).write_text(s)
            raise
    print(json.dumps({"commit": PIN,
                      "operation": "applied" if args.apply else "check-only",
                      "changed_files": len(paths),
                      "common_commit": COMMON_PIN,
                      "common_source_sha256": hashlib.sha256(common_patched.encode()).hexdigest(),
                      "patch_sha256": hashlib.sha256(diff.encode()).hexdigest(),
                      "patch_lines": len(diff.splitlines())}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (PatchError, subprocess.CalledProcessError, OSError) as e:
        raise SystemExit(f"Patch refused: {e}")
