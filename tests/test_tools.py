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
from unittest import mock
ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
run=module('run');validator=module('validate_capture')
class ToolTests(unittest.TestCase):
    def config(self):return json.loads((ROOT/'config/example.json').read_text())
    def test_packaged_apps_has_no_commands(self):
        data=json.loads((ROOT/'config/hdmi-apps.json').read_text())
        self.assertEqual(data,{'env':{},'apps':[{'name':'HDMI','cmd':'','image-path':''}]})
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
    def test_1440p90_gate_defaults_and_environment(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';c=self.config();c.pop('allow_1440p90_experiment',None)
            p.write_text(json.dumps(c));loaded=run.load(p)
            self.assertIs(loaded['allow_1440p90_experiment'],False)
            with mock.patch.dict(os.environ,{'RKMOON_ALLOW_1440P90_EXPERIMENT':'1'}):
                self.assertEqual(run.base_env(loaded,Path(d))['RKMOON_ALLOW_1440P90_EXPERIMENT'],'0')
            c['allow_1440p90_experiment']='true';p.write_text(json.dumps(c))
            with self.assertRaises(run.Refused):run.load(p)
            c['allow_1440p90_experiment']=True;p.write_text(json.dumps(c))
            self.assertEqual(run.base_env(run.load(p),Path(d))['RKMOON_ALLOW_1440P90_EXPERIMENT'],'1')
    def test_absolute_mouse_gate_defaults_and_environment(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';c=self.config();p.write_text(json.dumps(c))
            loaded=run.load(p)
            self.assertIs(loaded['allow_absolute_mouse'],False)
            with mock.patch.dict(os.environ,{'RKMOON_ALLOW_ABSOLUTE_MOUSE':'1'}):
                self.assertEqual(run.base_env(loaded,Path(d))['RKMOON_ALLOW_ABSOLUTE_MOUSE'],'0')
            c['allow_absolute_mouse']='true';p.write_text(json.dumps(c))
            with self.assertRaises(run.Refused):run.load(p)
            c['allow_absolute_mouse']=True;p.write_text(json.dumps(c))
            self.assertEqual(run.base_env(run.load(p),Path(d))['RKMOON_ALLOW_ABSOLUTE_MOUSE'],'0')
            c['input']['enabled']=True;p.write_text(json.dumps(c))
            self.assertEqual(run.base_env(run.load(p),Path(d))['RKMOON_ALLOW_ABSOLUTE_MOUSE'],'1')
    def test_audio_requires_explicit_hardware_capture(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';c=self.config()
            for device in ('default','pulse','hw:0\nBAD',''):
                c['audio']={'enabled':True,'alsa_device':device};p.write_text(json.dumps(c))
                with self.assertRaises(run.Refused):run.load(p)
            c['audio']={'enabled':True,'alsa_device':'hw:CARD=HDMI,DEV=0'};p.write_text(json.dumps(c))
            self.assertEqual(run.load(p)['audio']['alsa_device'],'hw:CARD=HDMI,DEV=0')
    def test_audio_disabled_does_not_inherit_device(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'config';p.write_text(json.dumps(self.config()))
            c=run.load(p)
            with mock.patch.dict(os.environ,{'RKMOON_AUDIO_DEVICE':'default'}):
                self.assertNotIn('RKMOON_AUDIO_DEVICE',run.base_env(c,Path(d)))
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
