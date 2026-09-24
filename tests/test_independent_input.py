# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,json,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('input_config',ROOT/'tools/input_config.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class IndependentInput(unittest.TestCase):
    def test_private_auth_no_legacy_paths(self):
        d=m.configuration('/opt/rkmoon-input/source','/etc/rkmoon-input','tester','fc000000.usb')
        self.assertNotIn('t6-kvm',json.dumps(d))
        self.assertTrue(d['kvmd']['auth']['enabled'])
        self.assertEqual(d['kvmd']['auth']['usc']['users'],['tester'])
        self.assertFalse(d['kvmd']['hid']['mouse']['absolute'])
        self.assertEqual(d['kvmd']['hid']['mouse_alt']['device'],'/dev/hidg2')
        self.assertTrue(d['otg']['devices']['hid']['mouse_alt']['start'])
        self.assertFalse(d['kvmd']['hid']['jiggler']['enabled'])
        self.assertNotIn('vnc',d)
        self.assertFalse(d['otg']['devices']['msd']['start'])
    def test_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as td:
            m.write_config('/opt/rkmoon-input/source',td,'tester','udc0')
            path=Path(td)/'main.yaml';before=path.read_bytes()
            with self.assertRaises(RuntimeError):m.write_config('/another',td,'another','udc1')
            self.assertEqual(path.read_bytes(),before)
            self.assertEqual((Path(td)/'htpasswd').read_text(),'')
if __name__=='__main__':unittest.main()
