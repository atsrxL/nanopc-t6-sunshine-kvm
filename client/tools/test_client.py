#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline source-pin and overlay safety regression (not a streaming test)."""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent / 'vendor/moonlight-qt'
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
            'app/settings/streamingpreferences.h': '#include <QQmlEngine>\n',
            'app/settings/streamingpreferences.cpp': overlay.PREFS_GET_ANCHOR,
            'app/streaming/input/input.h': '#include "backend/computermanager.h"',
            'app/streaming/input/input.cpp': overlay.GAMEPAD_MASK_ANCHOR,
            'app/streaming/audio/audio.cpp': overlay.AUDIO_GAIN_ANCHOR,
            'app/streaming/session.cpp': '#include <QSvgRenderer>\n',
            'app/backend/nvhttp.cpp': 'qInfo() << "Executing request:" << url.toString();',
        }
        for path, anchor in anchors.items():
            altered = dict(originals)
            self.assertEqual(altered[path].count(anchor), 1)
            altered[path] = altered[path].replace(anchor, '', 1)
            with self.assertRaises(overlay.PatchError, msg=path):
                overlay.make_changes(altered)
        session = changes['app/streaming/session.cpp']
        self.assertNotIn('RichPresenceManager', session)
        self.assertNotIn('QSvgRenderer', session)
        self.assertIn('LiStartConnection', session)
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
        self.assertIn('LiSendMouseMoveEvent', (REPO / 'app/streaming/input/mouse.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
