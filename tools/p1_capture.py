#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicitly authorized real HDMI capture; creates a NEW evidence directory and temperature/CPU samples."""
import argparse
import csv
import glob
import json
import os
from pathlib import Path
import signal
import subprocess
import time

def sample(pid):
    try:
        fields=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()
        cpu_ticks=int(fields[11])+int(fields[12]);rss_pages=int(fields[21])
    except (OSError,ValueError,IndexError):return None
    temps=[]
    for p in glob.glob('/sys/class/thermal/thermal_zone*/temp'):
        try:temps.append(int(Path(p).read_text()))
        except (OSError,ValueError):pass
    return {'monotonic_s':time.monotonic(),'cpu_ticks':cpu_ticks,'rss_bytes':rss_pages*os.sysconf('SC_PAGE_SIZE'),
            'temperature_millidegrees':temps}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--worker',type=Path,required=True);p.add_argument('--device',required=True);p.add_argument('--result',type=Path,required=True)
    p.add_argument('--codec',choices=['hevc','h264'],default='hevc');p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080);p.add_argument('--fps-x100',type=int,default=6000)
    p.add_argument('--bitrate',type=int,default=20000000);p.add_argument('--allow-copy',action='store_true')
    p.add_argument('--ack-capture-ownership',action='store_true');a=p.parse_args()
    if not a.ack_capture_ownership:raise SystemExit('No capture performed: --ack-capture-ownership requires an agreed exclusive capture window')
    if not 1<=a.seconds<=3600:raise SystemExit('duration must be 1..3600 seconds')
    a.result.mkdir(mode=0o700,parents=True,exist_ok=False)
    args=[str(a.worker.resolve()),'--device',a.device,'--codec',a.codec,'--seconds',str(a.seconds),'--width',str(a.width),'--height',str(a.height),
          '--fps-x100',str(a.fps_x100),'--bitrate',str(a.bitrate),'--gop','120','--idr-at','1','--output',str(a.result/f'capture.{a.codec}'),
          '--stats',str(a.result/'frames.csv'),'--ack-capture-ownership']
    if a.allow_copy:args+=['--allow-copy']
    (a.result/'command.json').write_text(json.dumps({'argv':args,'clock':'same-host CLOCK_MONOTONIC','cpu_ticks_per_second':os.sysconf('SC_CLK_TCK')},indent=2))
    with (a.result/'worker.log').open('x') as log,(a.result/'resources.jsonl').open('x') as resources:
        child=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
        stopped=False
        def stop(sig,frame):
            nonlocal stopped
            stopped=True
            if child.poll() is None:child.terminate()
        for sig in (signal.SIGTERM,signal.SIGINT):signal.signal(sig,stop)
        deadline=time.monotonic()+a.seconds+10
        while child.poll() is None:
            row=sample(child.pid)
            if row:resources.write(json.dumps(row)+'\n');resources.flush()
            if stopped or time.monotonic()>deadline:
                child.terminate()
                try:child.wait(3)
                except subprocess.TimeoutExpired:child.kill();child.wait()
                break
            time.sleep(.5)
        code=child.wait()
    (a.result/'exit.json').write_text(json.dumps({'exit':code,'interrupted':stopped,'hardware_acceptance':'not assigned'},indent=2))
    print(f'Capture process exit={code}; evidence={a.result}; independent decode/visual/client acceptance still required')
    return code
if __name__=='__main__':raise SystemExit(main())
