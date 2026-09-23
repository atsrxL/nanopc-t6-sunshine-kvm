# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import fcntl
import subprocess
import sys
import json
import os
from pathlib import Path
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
run=module('run');validator=module('validate_capture')
class ToolTests(unittest.TestCase):
    def config(self):return json.loads((ROOT/'config/example.json').read_text())
    def test_example_is_unprivileged_and_unauthorized(self):
        c=self.config();self.assertFalse(c['capture_ownership_authorized']);self.assertFalse(c['input']['enabled']);self.assertFalse(c['input']['exclusive_hid_authorized'])
    def test_config_unknown_fields(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';c=self.config();c['execute_shell']='rm';p.write_text(json.dumps(c))
            with self.assertRaises(run.Refused):run.load(p)
    def test_boolean_is_not_string(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';c=self.config();c['capture_ownership_authorized']='false';p.write_text(json.dumps(c))
            with self.assertRaises(run.Refused):run.load(p)
    def test_prepare_never_overwrites(self):
        with tempfile.TemporaryDirectory() as d:
            c=self.config();c['state_directory']=d;s=run.private_state(c);run.prepare(c,s)
            p=s/'sunshine.conf';p.write_text('custom local config');run.prepare(c,s);self.assertEqual(p.read_text(),'custom local config')
    def test_private_dir_permissions(self):
        with tempfile.TemporaryDirectory() as d:
            os.chmod(d,0o755);c=self.config();c['state_directory']=d
            with self.assertRaises(run.Refused):run.private_state(c)
    def test_stock_binary_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'stock';p.write_bytes(b'not a patched binary');p.chmod(0o700)
            with self.assertRaises(run.Refused):run.executable(p,run.MARKER)
    def test_no_input_no_release_request(self):
        with self.assertRaises(run.Refused):run.hid_command(self.config(),Path('/tmp'),True)
    def test_quantiles(self):
        q=validator.quantiles([0,10,20,30,40]);self.assertEqual(q['n'],5);self.assertEqual(q['p50'],20);self.assertEqual(q['max'],40)
    def test_empty_quantiles(self):self.assertEqual(validator.quantiles([]),{'n':0})

class StopRecoveryTests(unittest.TestCase):
    def configuration(self,folder):
        state=Path(folder)/'state';state.mkdir(mode=0o700)
        c=json.loads((ROOT/'config/example.json').read_text());c['state_directory']=str(state)
        c['input']['enabled']=True;c['input']['exclusive_hid_authorized']=True
        c['input']['kvmd_socket']='/NONEXISTENT-KVMD-MUST-NOT-BE-OPENED.sock'
        path=Path(folder)/'config.json';path.write_text(json.dumps(c))
        return state,path
    def invoke(self,path):
        return subprocess.run([sys.executable,str(ROOT/'tools/systemd_release.py'),'--config',str(path)],capture_output=True,text=True,timeout=3)
    def test_failed_start_without_owned_input_does_not_release(self):
        with tempfile.TemporaryDirectory() as d:
            state,path=self.configuration(d)
            self.assertEqual(self.invoke(path).returncode,0)
    def test_failed_second_start_does_not_release_active_owner(self):
        with tempfile.TemporaryDirectory() as d:
            state,path=self.configuration(d);marker=state/'hid-recovery-needed.json';marker.write_text('{}')
            with (state/'supervisor.lock').open('w') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
                self.assertEqual(self.invoke(path).returncode,0)
                self.assertTrue(marker.exists())
