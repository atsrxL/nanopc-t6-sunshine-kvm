# SPDX-License-Identifier: GPL-3.0-or-later
"""Transformation unit tests on small source fixtures; NOT full Sunshine patch application."""
import importlib.util
from pathlib import Path
import unittest
p=Path(__file__).resolve().parents[1]/'tools/apply_sunshine.py'
spec=importlib.util.spec_from_file_location('apply_sunshine',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class OverlayTests(unittest.TestCase):
    def test_platform_sources_can_find_bridge_header(self):
        cmake=m.make_changes({'CMakeLists.txt':''})['CMakeLists.txt']
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}"',cmake)
        self.assertNotIn('"${CMAKE_CURRENT_SOURCE_DIR}/src"',cmake)
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/src/rkmoon/include"',cmake)
    def test_once(self):self.assertEqual(m.once('abc','b','x'),'axc')
    def test_missing_anchor(self):
        with self.assertRaises(m.PatchError):m.once('abc','q','x')
    def test_duplicate_anchor(self):
        with self.assertRaises(m.PatchError):m.once('aa','a','x')
    def test_multiline_signature(self):
        s='  void capture(\n    safe::mail_t mail,\n    config_t config,\n    void *channel_data\n  ) {\n    original();\n  }\n'
        r=m.at_entry(s,'  void capture(safe::mail_t mail, config_t config, void *channel_data)','    hook();\n')
        self.assertLess(r.index('hook();'),r.index('original();'))
    def test_changed_signature(self):
        with self.assertRaises(m.PatchError):m.at_entry('int f(int x) {\n}', 'int f(double x)','hook();')
    def test_queue_not_replaced(self):
        anchor='    explicit queue_t(std::uint32_t max_elements = 32, overflow_policy_e overflow = overflow_policy_e::drop_oldest):'
        r=m.make_changes({'src/thread_safe.h':anchor})['src/thread_safe.h']
        self.assertIn('rkmoon_try_raise',r);self.assertIn('_queue.size() >= limit',r);self.assertNotIn('_queue.clear()',r)
    def test_audio_is_explicitly_disabled(self):
        s='// local includes\n  void capture(safe::mail_t mail, config_t config, void *channel_data) {\noriginal();\n}\n'
        r=m.make_changes({'src/audio.cpp':s})['src/audio.cpp']
        self.assertIn('mail::shutdown',r);self.assertIn('return;',r)
    def test_rtsp_admission_precedes_allocation(self):
        s='// local includes\n    auto stream_session = stream::session::alloc(config, session);\n'
        r=m.make_changes({'src/rtsp.cpp':s})['src/rtsp.cpp']
        self.assertLess(r.index('session_count()'),r.index('stream::session::alloc'))
        self.assertIn('453',r)
    def test_commit_fixed(self):self.assertEqual(m.PIN,'63d35f702ee9e362e43263742981836ec0710384')
