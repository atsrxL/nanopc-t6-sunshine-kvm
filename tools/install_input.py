#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install independent pinned kvmd input backend; never starts or replaces a gadget."""
import argparse,os,shutil,subprocess,sys
from pathlib import Path
from prepare_input import prepare
from input_config import write_config
ROOT=Path(__file__).resolve().parents[1]
def run(*args):subprocess.run(args,check=True)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True,type=Path);p.add_argument('--user',required=True);p.add_argument('--udc',required=True)
    a=p.parse_args()
    if os.getuid()!=0:raise RuntimeError('Installer requires root; daemon runs unprivileged')
    if sys.version_info[:2]!=(3,13):raise RuntimeError('Pinned kvmd requires Python 3.13')
    prefix=Path('/opt/rkmoon-input');state=Path('/etc/rkmoon-input')
    if prefix.exists() or state.exists():raise RuntimeError('Existing installation; migrate explicitly')
    run('id',a.user)
    prepare(a.source)
    prefix.mkdir();shutil.copytree(a.source,prefix/'source')
    run(sys.executable,'-m','venv','--system-site-packages',str(prefix/'venv'))
    shutil.copy2(ROOT/'tools/input_runtime.py',prefix/'runtime.py')
    write_config(prefix/'source',state,a.user,a.udc)
    if subprocess.run(['getent','group','rkmoon-input'],stdout=subprocess.DEVNULL).returncode:run('groupadd','--system','rkmoon-input')
    if subprocess.run(['id','rkmoon-input'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
        run('useradd','--system','--gid','rkmoon-input','--home-dir','/var/lib/rkmoon-input','--shell','/usr/sbin/nologin','rkmoon-input')
    run('usermod','-a','-G','rkmoon-input',a.user)
    run('chown','-R','root:rkmoon-input',str(state));state.chmod(0o750)
    for f in state.iterdir():
        if f.is_file():f.chmod(0o640)
    with (prefix/'config-validation.yaml').open('w') as out:
        subprocess.run([str(prefix/'venv/bin/python'),str(prefix/'runtime.py'),'check','--source',str(prefix/'source'),'--config',str(state/'main.yaml')],stdout=out,check=True)
    for name in ['rkmoon-gadget.service','rkmoon-input.service']:
        shutil.copy2(ROOT/'systemd'/name,Path('/etc/systemd/system')/name)
    run('systemctl','daemon-reload')
    print('Installed; NOT started. Check exclusive UDC ownership, then enable --now rkmoon-input.service.')
if __name__=='__main__':main()
