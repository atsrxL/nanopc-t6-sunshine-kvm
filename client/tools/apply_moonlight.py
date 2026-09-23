#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Commit-locked, fail-closed moonlight-qt overlay for the minimal KVM client. Default operation is a dry run.

The overlay removes unshipped controller/QML/Discord/SVG features, disables absolute mouse
switching, redacts HTTP request/response logging and adds post-Opus mute/volume attenuation.
Video transport, decoding, pairing crypto and encrypted input packet implementations are retained.
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
            # ComputerManager pulls in qmdnsengine (mDNS discovery), which this client does not use.
            text = once(text, '#include "backend/computermanager.h"',
                        '#include "backend/nvcomputer.h" // RKMoon: was backend/computermanager.h (no mDNS discovery)')
        elif path == "app/streaming/input/input.cpp":
            text = once(text, GAMEPAD_INIT_ANCHOR, GAMEPAD_INIT_REPLACEMENT)
            text = once(text, GAMEPAD_MASK_ANCHOR, GAMEPAD_MASK_REPLACEMENT)
            text = once(text, GAMEPAD_QUIT_ANCHOR, GAMEPAD_QUIT_REPLACEMENT)
            text = once(text, '    m_SpecialKeyCombos[KeyComboToggleMouseMode].enabled = true;',
                        '    m_SpecialKeyCombos[KeyComboToggleMouseMode].enabled = false; // Relative HID only')
        elif path == "app/streaming/audio/audio.cpp":
            text = once(text, '#include "renderers/sdl.h"\n',
                        '#include "renderers/sdl.h"\n#include "rkmoon_audio_control.h"\n')
            text = once(text, AUDIO_GAIN_ANCHOR, AUDIO_GAIN_REPLACEMENT)
        elif path == "app/streaming/session.cpp":
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
            text = once(text, '#include "nvcomputer.h"\n', '#include "nvcomputer.h"\n#include "rkmoon_http_log.h"\n')
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
    dirty = git(repo, "status", "--porcelain").splitlines()
    for line in dirty:
        path = line[3:]
        if path not in FILES or (repo / path).read_text() not in (original[path], changes[path]):
            raise PatchError("checkout contains unrelated edits; refuse to overwrite local work")
    if args.apply and any((repo / p).read_text() != original[p] for p in FILES):
        raise PatchError("apply requires all original files; use a fresh pinned checkout")
    diff = "".join("".join(difflib.unified_diff(original[p].splitlines(True),
                                                changes[p].splitlines(True),
                                                fromfile="a/" + p, tofile="b/" + p)) for p in FILES)
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
                      "changed_files": len(FILES),
                      "patch_sha256": hashlib.sha256(diff.encode()).hexdigest(),
                      "patch_lines": len(diff.splitlines())}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (PatchError, subprocess.CalledProcessError, OSError) as e:
        raise SystemExit(f"Patch refused: {e}")
