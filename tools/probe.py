#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only baseline. No streaming, SET ioctls, HID events, modesets, credentials or OS changes."""
import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

def read(path,limit=8192):
    try:return Path(path).read_bytes()[:limit].replace(b'\x00',b' ').decode(errors='replace').strip()
    except OSError:return None

def cmd(args):
    if not shutil.which(args[0]):return {'unavailable':args[0]}
    try:
        r=subprocess.run(args,capture_output=True,text=True,timeout=8)
        return {'exit':r.returncode,'stdout':r.stdout[:16000],'stderr':r.stderr[:2000]}
    except (OSError,subprocess.TimeoutExpired):return {'error':'unavailable or timeout'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--video',action='append',default=[]);p.add_argument('--output',type=Path)
    a=p.parse_args();report={'schema':1,'read_only':True,'kernel':platform.release(),'machine':platform.machine(),
        'model':read('/proc/device-tree/model'),'os_release':read('/etc/os-release'),'memory':read('/proc/meminfo'),
        'thermal':[], 'video':[], 'devices':[], 'gadget_descriptors':[], 'service_state':{},'tools':{}}
    for t in glob.glob('/sys/class/thermal/thermal_zone*'):
        report['thermal'].append({'type':read(t+'/type'),'millidegrees_c':read(t+'/temp')})
    nodes=a.video or sorted(glob.glob('/dev/video*'))
    for node in nodes:
        if not node.startswith('/dev/') or '\n' in node:raise SystemExit('Expected a local /dev video path')
        item={'path':node}
        # --all includes device names/driver/timings, not application configuration or login secrets.
        for flag in ['--all','--query-dv-timings','--get-fmt-video','--list-formats-ext']:
            item[flag]=cmd(['v4l2-ctl','--device',node,flag])
        item['open_owners']=cmd(['fuser',node]) # PIDs only; do not stop owners.
        report['video'].append(item)
    for pattern in ['/dev/mpp_service','/dev/rga','/dev/dri/renderD*','/dev/hidg*','/dev/kvmd-hid-*','/dev/dma_heap/*']:
        for path in sorted(glob.glob(pattern)):
            st=os.stat(path);report['devices'].append({'path':path,'mode':oct(st.st_mode&0o777),'uid':st.st_uid,'gid':st.st_gid,'readable':os.access(path,os.R_OK),'writable':os.access(path,os.W_OK)})
    # Descriptors, not USB serial numbers or contents of HID events.
    for path in glob.glob('/sys/kernel/config/usb_gadget/*/functions/hid.*/report_desc'):
        try:
            data=Path(path).read_bytes();base=Path(path).parent
            report['gadget_descriptors'].append({'function':base.name,'report_length':read(base/'report_length'),
                'descriptor_length':len(data),'descriptor_sha256':hashlib.sha256(data).hexdigest()})
        except OSError:pass
    for unit in ['kvmd.service','kvmd-vnc.service','t6-kvm.service',
                 't6-kvmd.service','t6-kvmd-vnc.service','t6-kvm-panel.service','sunshine.service']:
        report['service_state'][unit]=cmd(['systemctl','is-active',unit])
    for tool,args in [('v4l2-ctl',['--version']),('ffmpeg',['-version']),('cmake',['--version']),('g++',['--version']),('pkg-config',['--modversion','rockchip_mpp'])]:
        r=cmd([tool,*args]);
        if 'stdout' in r:r['stdout']='\n'.join(r['stdout'].splitlines()[:3])
        report['tools'][tool]=r
    # Do not dump env, command line, EDID payload, hostname, IP, MAC, configs or /proc/*/cmdline.
    data=json.dumps(report,ensure_ascii=False,indent=2)+'\n'
    if a.output:
        with a.output.open('x') as f:f.write(data)
        a.output.chmod(0o600)
    else:print(data,end='')
if __name__=='__main__':main()
