# SPDX-License-Identifier: GPL-3.0-or-later
"""Transformation unit tests on small source fixtures; NOT full Sunshine patch application."""
import importlib.util
from pathlib import Path
import unittest
import subprocess
p=Path(__file__).resolve().parents[1]/'tools/apply_sunshine.py'
spec=importlib.util.spec_from_file_location('apply_sunshine',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class OverlayTests(unittest.TestCase):
    def test_platform_sources_can_find_bridge_header(self):
        cmake=m.make_changes({'CMakeLists.txt':'# setup compile definitions\n# target definitions\n'})['CMakeLists.txt']
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
    def test_audio_uses_alsa_opus_bridge(self):
        s='// local includes\n  void capture(safe::mail_t mail, config_t config, void *channel_data) {\noriginal();\n}\n'
        r=m.make_changes({'src/audio.cpp':s})['src/audio.cpp']
        self.assertIn('rkmoon_sunshine::audio_capture(',r)
        self.assertLess(r.index('audio_capture('),r.index('original();'))
    def test_web_ui_target_removed(self):
        s='#WebUI build\nfind_program(NPM npm REQUIRED)\n# docs\n'
        r=m.make_changes({'cmake/targets/common.cmake':s})['cmake/targets/common.cmake']
        self.assertNotIn('find_program(NPM',r)
        self.assertIn('# docs',r)
    def test_no_desktop_capture_backend_is_required(self):
        fixture='if(NOT ${CUDA_FOUND}\n        AND NOT ${LIBDRM_FOUND}'
        changed=m.make_changes({'cmake/compile_definitions/linux.cmake':fixture})['cmake/compile_definitions/linux.cmake']
        self.assertIn('if(NOT RKMOON_MINIMAL_BUILD AND NOT ${CUDA_FOUND}',changed)
    def test_no_desktop_platform_init(self):
        fixture='// local includes\n  std::unique_ptr<deinit_t> init() {\noriginal();\n}\n'
        changed=m.make_changes({'src/platform/linux/misc.cpp':fixture})['src/platform/linux/misc.cpp']
        self.assertLess(changed.index('rkmoon_sunshine::enabled()'),changed.index('original();'))
        self.assertIn('make_unique<deinit_t>()',changed)
    def test_pairing_can_cancel_wait(self):
        source=(Path(__file__).resolve().parents[1]/'vendor/sunshine/src/nvhttp.cpp')
        # Fixture is the pinned source, not a fuzzy patch of an unknown version.
        if source.exists():
            original=subprocess.check_output(['git','-C',str(source.parents[1]),'show',m.PIN+':src/nvhttp.cpp'],text=True)
            changed=m.make_changes({'src/nvhttp.cpp':original})['src/nvhttp.cpp']
            self.assertIn('stop.stop_requested()',changed)
            self.assertIn('std::chrono::seconds(30)',changed)
    def test_rtsp_admission_precedes_allocation(self):
        s='// local includes\n    auto stream_session = stream::session::alloc(config, session);\n'
        r=m.make_changes({'src/rtsp.cpp':s})['src/rtsp.cpp']
        self.assertLess(r.index('session_count()'),r.index('stream::session::alloc'))
        self.assertIn('453',r)
    def test_commit_fixed(self):self.assertEqual(m.PIN,'63d35f702ee9e362e43263742981836ec0710384')
