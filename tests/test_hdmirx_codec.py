# SPDX-License-Identifier: GPL-2.0-only
"""Pinned kernel transformation checks; no device access or module insertion."""
import importlib.util
import os
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('codec_patch',ROOT/'tools/prepare_hdmirx_codec.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class HdmiRxCodecTests(unittest.TestCase):
    def source(self):
        path=os.environ.get('RKMOON_HDMI_CODEC_SOURCE')
        if not path:self.skipTest('exact kernel source fixture not provided')
        return Path(path).read_text()
    def test_unknown_source_refused(self):
        with self.assertRaises(ValueError):module.transform('unknown kernel code')
    def test_renamed_rx_only_driver(self):
        result=module.transform(self.source())
        self.assertIn('.name = "rkmoon-hdmirx-codec",',result)
        self.assertNotIn('MODULE_ALIAS("platform:" HDMI_CODEC_DRV_NAME)',result)
        self.assertNotIn('EXPORT_SYMBOL',result)
        self.assertIn('if (!is_hdmirx)\n\t\treturn -ENODEV;',result)
        self.assertLess(result.index('if (!is_hdmirx)\n\t\treturn -ENODEV;'),result.index('hcp = devm_kzalloc'))
        self.assertEqual(result.count('if (!is_hdmirx) {'),2)
        for compatible in module.COMPATIBLES:self.assertIn('"'+compatible+'"',result)
    def test_generic_patch_keeps_tx_policy(self):
        result=module.transform(self.source(),False)
        self.assertIn('.name = HDMI_CODEC_DRV_NAME,',result)
        self.assertNotIn('Standalone workaround',result)
        self.assertEqual(result.count('if (!is_hdmirx) {'),2)
        self.assertEqual(result.count('daidrv[i].capture.channels_min = 0;'),2)
        self.assertIn('dev->parent &&',result)
