#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline source-pin and overlay safety regression (not a streaming test)."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
# The pinned checkout lives beside the project by default. RKMOON_MOONLIGHT_DIR points at
# an equally pinned checkout elsewhere; the commit and submodule assertions below still
# decide whether it is acceptable.
REPO = Path(os.environ.get('RKMOON_MOONLIGHT_DIR', ROOT.parent / 'vendor/moonlight-qt'))
spec = importlib.util.spec_from_file_location('overlay', ROOT / 'tools/apply_moonlight.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class OverlayTest(unittest.TestCase):
    def test_sources(self):
        lock = json.loads((ROOT / 'sources.lock.json').read_text())
        self.assertEqual(subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip(),
                         lock['sources']['moonlight-qt']['commit'])
        for path, commit in lock['submodules_required'].items():
            self.assertEqual(subprocess.check_output(['git', '-C', str(REPO / path), 'rev-parse', 'HEAD'], text=True).strip(), commit)

    def test_exact_overlay_and_fail_closed(self):
        originals = {path: subprocess.check_output(['git', '-C', str(REPO), 'show', 'HEAD:' + path], text=True)
                     for path in overlay.FILES}
        changes = overlay.make_changes(originals)
        for path in overlay.FILES:
            self.assertEqual((REPO / path).read_text(), changes[path], path)
        anchors = {
            'app/settings/streamingpreferences.h': ['#include <QQmlEngine>\n'],
            'app/settings/streamingpreferences.cpp': [overlay.PREFS_GET_ANCHOR],
            'app/streaming/input/input.h': ['#include "backend/computermanager.h"'],
            'app/streaming/input/input.cpp': [overlay.GAMEPAD_MASK_ANCHOR, overlay.CAPTURE_ANCHOR],
            'app/streaming/input/keyboard.cpp': [overlay.CURSOR_ANCHOR],
            'app/streaming/input/mouse.cpp': [overlay.POINTER_CLAMP_ANCHOR, overlay.POINTER_INSIDE_ANCHOR],
            'app/streaming/input/abstouch.cpp': [overlay.POINTER_TOUCH_ANCHOR, 'if (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS) {'],
            'app/streaming/audio/audio.cpp': [overlay.AUDIO_GAIN_ANCHOR],
            'app/streaming/session.cpp': ['#include <QSvgRenderer>\n', overlay.PUMP_ANCHOR, overlay.TERMINATE_ANCHOR],
            'app/backend/nvhttp.cpp': ['qInfo() << "Executing request:" << url.toString();',
                                       overlay.HTTP_AUTH_ANCHOR,
                                       overlay.HTTPS_SCHEME_ANCHOR,
                                       overlay.HTTPS_ADDRESS_ANCHOR,
                                       overlay.HTTPS_PORT_ANCHOR],
        }
        for path, path_anchors in anchors.items():
            for anchor in path_anchors:
                altered = dict(originals)
                self.assertEqual(altered[path].count(anchor), 1)
                altered[path] = altered[path].replace(anchor, '', 1)
                with self.assertRaises(overlay.PatchError, msg=path):
                    overlay.make_changes(altered)
        session = changes['app/streaming/session.cpp']
        self.assertNotIn('RichPresenceManager', session)
        self.assertNotIn('QSvgRenderer', session)
        self.assertIn('LiStartConnection', session)
        self.assertNotIn(overlay.TERMINATE_ANCHOR, session)
        self.assertIn('internalQuitPending.exchange(false)', session)
        self.assertNotIn('internalQuitEvent', session)
        self.assertIn('case SDL_USEREVENT:', session)
        self.assertIn('RkmoonSessionControl::stop();', session)
        keyboard = changes['app/streaming/input/keyboard.cpp']
        self.assertIn('event.type = SDL_QUIT;', keyboard)
        self.assertNotIn('m_AbsoluteMouseMode =', overlay.CURSOR_REPLACEMENT)
        self.assertNotIn('m_AbsoluteTouchMode', overlay.CURSOR_REPLACEMENT)
        self.assertNotIn('if (LiGetHostFeatureFlags() & LI_FF_PEN_TOUCH_EVENTS)', changes['app/streaming/input/abstouch.cpp'])
        self.assertIn('emulateAbsoluteFingerEvent(event);', changes['app/streaming/input/abstouch.cpp'])
        self.assertIn('RkmoonAuth::prepareSessionUrl(url, command);', changes['app/backend/nvhttp.cpp'])
        audio = changes['app/streaming/audio/audio.cpp']
        self.assertIn('opus_multistream_decode_float', audio)
        self.assertIn('opus_multistream_decode(', audio)
        self.assertIn('RkmoonAudioControl::applyGain', audio)
        inp = changes['app/streaming/input/input.cpp']
        self.assertIn('m_GamepadMask = 0;', inp)
        self.assertIn('KeyComboToggleMouseMode].enabled = false;', inp)
        http = changes['app/backend/nvhttp.cpp']
        self.assertNotIn('<< url.toString()', http)
        self.assertNotIn('<< "Launch response:" << response', http)
        # The password credential is attached here, after upstream installs its own SSL
        # configuration, so the pinned CA list and Authorization header are not overwritten.
        self.assertIn('#include "rkmoon_auth.h"', http)
        self.assertIn('RkmoonAuth::prepareRequest(request, url);', http)
        # No client certificate is installed, so no identity key is ever generated.
        self.assertNotIn('IdentityManager::get()', http)
        self.assertNotIn('setSslConfiguration', http)
        # Every endpoint, including the upstream "HTTPS" call sites, is one plaintext origin.
        self.assertNotIn('m_BaseUrlHttps.setScheme("https")', http)
        self.assertIn('m_BaseUrlHttps.setScheme("http")', http)
        self.assertIn('m_BaseUrlHttps.setPort(m_BaseUrlHttp.port());', http)
        self.assertIn('LiSendMouseMoveEvent', (REPO / 'app/streaming/input/mouse.cpp').read_text())

    def test_actual_pinned_common_has_no_connection_jiggle(self):
        original, changed = overlay.common_change(REPO)
        self.assertEqual((REPO / overlay.COMMON_FILE).read_text(), changed)
        self.assertIn('ListenerCallbacks.connectionStarted();', changed)
        self.assertNotIn('LiSendMouseMoveEvent(', changed)
        self.assertIn('LiSendMouseMoveEvent(1, 1);', original)
        with self.assertRaises(overlay.PatchError):
            overlay.once(original.replace('PltSleepMs(10);', 'PltSleepMs(11);'), overlay.COMMON_WAKE, '')

    def test_client_never_pairs_or_persists_a_password(self):
        project = (ROOT / 'app/rkmoon-app.pro').read_text()
        # GameStream PIN pairing is not compiled at all; the server has no /pair route.
        self.assertNotIn('nvpairingmanager', project)
        self.assertNotIn('identitymanager', project)
        self.assertIn('src/rkmoon_auth.cpp', project)
        for name in ('app/src/kvmhost.cpp', 'app/src/kvmwindow.cpp', 'app/src/kvmconfig.cpp',
                     'app/src/kvmconfig.h', 'app/src/kvmhost.h'):
            source = (ROOT / name).read_text()
            self.assertNotIn('NvPairingManager', source, name)
            self.assertNotIn('nvpairingmanager', source, name)
        # The password has no persisted settings key and no QSettings write path.
        config = (ROOT / 'app/src/kvmconfig.cpp').read_text()
        self.assertNotIn('password', config.lower())
        # Only the memory-resident credential module may put the password on the wire.
        for name in ('app/src/kvmhost.cpp', 'app/src/kvmwindow.cpp', 'app/src/kvmconfig.cpp'):
            self.assertNotIn('setRawHeader', (ROOT / name).read_text(), name)
        auth = (ROOT / 'app/src/rkmoon_auth.cpp').read_text()
        self.assertIn('ManualRedirectPolicy', auth)
        self.assertNotIn('QSettings', auth)
        # The credential is scoped to plaintext HTTP only; no certificate state remains.
        self.assertIn('QStringLiteral("http")', auth)
        self.assertNotIn('QSslCertificate', auth)
        for name in ('app/src/kvmconfig.h', 'app/src/kvmconfig.cpp', 'app/src/kvmhost.h'):
            self.assertNotIn('serverCert', (ROOT / name).read_text(), name)


if __name__ == '__main__':
    unittest.main()
