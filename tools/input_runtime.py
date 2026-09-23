#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Own only the rkmoon USB gadget; run pinned kvmd without any VNC installation."""
import argparse,json,os,subprocess,sys,grp,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['start','stop','serve','check','ready'])
    p.add_argument('--source',required=True,type=Path)
    p.add_argument('--config',required=True,type=Path)
    a=p.parse_args();cfg=json.loads(a.config.read_text());g=cfg['otg']['gadget']
    if g!='rkmoon':raise RuntimeError('Only project-owned rkmoon gadget is allowed')
    udc=cfg['otg']['udc'];root=Path('/sys/kernel/config/usb_gadget');target=root/g
    args=['--main-config',str(a.config),'--legacy-auth-config',str(a.config.parent/'empty.yaml'),
          '--override-dir',str(a.config.parent/'override.d'),'--override-config',str(a.config.parent/'empty.yaml')]
    env=dict(os.environ,PYTHONPATH=str(a.source))
    def run(module,tail):
        code=f'from {module} import main; main()'
        if module=='kvmd.apps.otg':
            code='from kvmd.apps import init; from kvmd.apps.otg import _cmd_'+tail[0]+'; _cmd_'+tail[0]+'(init(load_hid=True,load_atx=True,load_msd=True).config)'
            tail=[]
        subprocess.run([sys.executable,'-c',code,*args,*tail],env=env,check=True)
    if a.action=='ready':
        import socket
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            try:
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as sock:
                    sock.settimeout(1);sock.connect(cfg['kvmd']['server']['unix'])
                return
            except OSError:time.sleep(.1)
        raise RuntimeError('Input socket did not become ready')
    if a.action=='serve':
        os.environ['PYTHONPATH']=str(a.source)
        os.execve(sys.executable,[sys.executable,'-c','from kvmd.apps.kvmd import main; import kvmd.htserver; kvmd.htserver.run_app=__import__("functools").partial(kvmd.htserver.run_app,access_log=None); main()',*args,'--run'],os.environ)
    if a.action=='check':
        run('kvmd.apps.kvmd',['--dump-config']);return
    if os.getuid()!=0:raise RuntimeError('Gadget operations require root; daemon does not')
    if not (Path('/sys/class/udc')/udc).exists():raise RuntimeError('Configured UDC does not exist')
    if a.action=='start':
        grp.getgrnam('rkmoon-input')
        if not (a.config.parent/'empty.yaml').is_file():raise RuntimeError('Missing independent configuration')
        for other in root.iterdir():
            if other!=target and (other/'UDC').read_text().strip()==udc:
                raise RuntimeError(f'UDC is owned by {other.name}; explicit migration required')
        if target.exists():raise RuntimeError('Existing rkmoon gadget; stop it explicitly before recreation')
        run('kvmd.apps.otg',['start'])
        gid=grp.getgrnam('rkmoon-input').gr_gid
        # Resolve actual HID minors: an unbound old gadget may still own hidg0/1.
        devices=[]
        for function in ['hid.usb0','hid.usb1']:
            major,minor=map(int,(target/'functions'/function/'dev').read_text().split(':'))
            node=Path('/dev')/f'hidg{minor}'
            for _ in range(30):
                if node.exists():break
                time.sleep(.1)
            if not node.exists() or node.stat().st_rdev!=os.makedev(major,minor):raise RuntimeError('HID node mismatch')
            os.chown(node,0,gid);node.chmod(0o660);devices.append(str(node))
        cfg['kvmd']['hid']['keyboard']['device']=devices[0]
        cfg['kvmd']['hid']['mouse']['device']=devices[1]
        a.config.write_text(json.dumps(cfg,indent=2)+'\n')
    else:
        if not target.exists():return
        if (target/'strings/0x409/serialnumber').read_text().strip()!='RKMOON001':raise RuntimeError('Gadget ownership mismatch')
        if (target/'UDC').read_text().strip() not in ['',udc]:raise RuntimeError('Gadget UDC mismatch')
        run('kvmd.apps.otg',['stop'])
if __name__=='__main__':main()
