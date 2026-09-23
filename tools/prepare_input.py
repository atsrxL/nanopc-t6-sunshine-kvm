#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare pinned upstream kvmd for input-only use without VNC dependencies."""
import argparse
import subprocess
from pathlib import Path
PIN = '78ff181e95b14327831441d58f2f7f4cb2181cde'
def prepare(source):
    actual = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if actual != PIN:
        raise RuntimeError('Unexpected kvmd revision')
    p=source/'kvmd/clients/streamer.py'
    text=p.read_text()
    marker='# RKMOON_INPUT_ONLY optional memsink'
    health=source/'kvmd/apps/kvmd/info/health.py'
    h=health.read_text()
    anchor_health='        cmd = [*self.__vcgencmd_cmd, arg]'
    if '# RKMOON non-Raspberry-Pi health' not in h:
        if h.count(anchor_health)!=1:raise RuntimeError('Health anchor changed')
        h=h.replace(anchor_health,'        # RKMOON non-Raspberry-Pi health\n        if self.__vcgencmd_cmd == ["/bin/false"]:\n            return None\n'+anchor_health)
        health.write_text(h)
    if marker in text:
        return
    anchor='            with ustreamer.Memsink(**self.__kwargs) as sink:'
    if text.count('import ustreamer\n') != 1 or text.count(anchor) != 1:
        raise RuntimeError('Pinned streamer anchors changed')
    text=text.replace('import ustreamer\n',marker+'\ntry:\n    import ustreamer\nexcept ModuleNotFoundError as exc:\n    if exc.name != "ustreamer":\n        raise\n    ustreamer = None\n')
    text=text.replace(anchor,'            if ustreamer is None:\n                raise StreamerPermError("Memsink unavailable in input-only installation")\n'+anchor)
    p.write_text(text)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    prepare(parser.parse_args().source)
