#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate an RX-only out-of-tree codec from the exact installed kernel revision.
Never modifies the kernel, device bindings, or a running machine. No force-load.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

REV='95e85f6cb496c75807c5b16f158853578e7e7d1b'
SHA='7035b4cc5159312134c3b172a55921f8f16e4cb6a1136626f4d1e381759be761'
DRIVER='rkmoon-hdmirx-codec'
COMPATIBLES=('rockchip,rk3588-hdmirx-ctrler','rockchip,hdmirx-ctrler')

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('pinned codec anchor mismatch')
    return text.replace(old,new,1)

def transform(source,external=True):
    if hashlib.sha256(source.encode()).hexdigest()!=SHA:
        raise ValueError('codec source does not match the exact installed kernel revision')
    if 'EXPORT_SYMBOL' in source:raise ValueError('unexpected exported symbols; stop before loading duplicate codec')
    text=once(source,'#include <linux/module.h>','#include <linux/module.h>\n#include <linux/of.h>')
    helper='''/* HDMI RX shares this codec with TX. Never infer direction from I2S alone. */
static bool hdmi_codec_is_hdmirx(struct device *dev)
{
	return dev->parent &&
		(of_device_is_compatible(dev->parent->of_node,
					 "rockchip,rk3588-hdmirx-ctrler") ||
		 of_device_is_compatible(dev->parent->of_node,
					 "rockchip,hdmirx-ctrler"));
}

'''
    text=once(text,'static int hdmi_codec_probe(struct platform_device *pdev)',helper+'static int hdmi_codec_probe(struct platform_device *pdev)')
    text=once(text,'\tint dai_count, i = 0;\n\tint ret;\n',
        '\tint dai_count, i = 0;\n\tint ret;\n\tbool is_hdmirx = hdmi_codec_is_hdmirx(dev);\n'+
        ('\n\t/* Standalone workaround must never bind a TX codec, even by override. */\n\tif (!is_hdmirx)\n\t\treturn -ENODEV;\n' if external else ''))
    pair='\t\tdaidrv[i].capture.channels_min = 0;\n\t\tdaidrv[i].capture.channels_max = 0;'
    if text.count(pair)!=2:raise ValueError('expected precisely the I2S and SPDIF regression sites')
    text=text.replace(pair,'\t\tif (!is_hdmirx) {\n\t\t\tdaidrv[i].capture.channels_min = 0;\n\t\t\tdaidrv[i].capture.channels_max = 0;\n\t\t}')
    if external:
        text=once(text,'.name = HDMI_CODEC_DRV_NAME,','.name = "'+DRIVER+'",')
        text=once(text,'MODULE_ALIAS("platform:" HDMI_CODEC_DRV_NAME);','MODULE_ALIAS("platform:'+DRIVER+'");')
        text=once(text,'MODULE_DESCRIPTION("HDMI Audio Codec Driver");','MODULE_DESCRIPTION("RKMoon RX-only HDMI codec capture repair for 6.1.115-vendor-rk35xx");')
    return text

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();source=a.source.read_text();a.output.mkdir(parents=True,exist_ok=True)
    if any(a.output.iterdir()):raise SystemExit('output must be empty; preserve prior build evidence')
    result=transform(source)
    (a.output/'rkmoon_hdmirx_codec.c').write_text(result)
    (a.output/'Makefile').write_text('obj-m += rkmoon_hdmirx_codec.o\n')
    generic=transform(source,False)
    patch=''.join(difflib.unified_diff(source.splitlines(True),generic.splitlines(True),fromfile='a/sound/soc/codecs/hdmi-codec.c',tofile='b/sound/soc/codecs/hdmi-codec.c'))
    (a.output/'upstream-rx-capture.patch').write_text(patch)
    (a.output/'source-manifest.json').write_text(json.dumps({'kernel_revision':REV,'original_source_sha256':SHA,'module_source_sha256':hashlib.sha256(result.encode()).hexdigest(),'driver':DRIVER,'rx_parent_compatibles':COMPATIBLES,'exports':[],'module_name':'rkmoon_hdmirx_codec','deployment':'manual driver_override, codec.5 only; never autoload or force-load'},indent=2)+'\n')

if __name__=='__main__':main()
