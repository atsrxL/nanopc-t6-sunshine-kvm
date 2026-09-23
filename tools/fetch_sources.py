#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download fixed source revisions into NEW independent directories; never provision the OS."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--destination',type=Path,default=ROOT/'vendor')
    p.add_argument('--component',choices=['sunshine','mpp','kvmd','all'],default='all')
    a=p.parse_args();lock=json.loads((ROOT/'sources.lock.json').read_text())['sources']
    a.destination.mkdir(parents=True,exist_ok=True)
    for name in (['mpp','sunshine','kvmd'] if a.component=='all' else [a.component]):
        target=a.destination/name;entry=lock[name]
        if target.exists():raise SystemExit(f'Refusing existing directory: {target}; verify/reuse manually, never reset it')
        target.mkdir();subprocess.run(['git','init',str(target)],check=True)
        def git(*args):subprocess.run(['git','-C',str(target),*args],check=True)
        git('remote','add','origin',entry['url']);git('fetch','--depth','1','origin',entry['commit']);git('checkout','--detach','FETCH_HEAD')
        actual=subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip()
        if actual!=entry['commit']:raise SystemExit('Pinned commit mismatch')
        if name=='sunshine':git('submodule','update','--init','--recursive','--depth','1')
        print(f'{name}: {actual}; no install or service operation performed')
if __name__=='__main__':main()
