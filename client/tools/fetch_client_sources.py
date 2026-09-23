#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download the fixed moonlight-qt revision into a NEW directory; never provision the OS."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'sources.lock.json').read_text())


def git(repo, *args):
    subprocess.run(['git', '-C', str(repo), *args], check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--destination', type=Path, default=ROOT.parent / 'vendor' / 'moonlight-qt')
    a = p.parse_args()
    entry = LOCK['sources']['moonlight-qt']
    target = a.destination
    if target.exists():
        raise SystemExit(f'Refusing existing directory: {target}; verify or reuse it manually, never reset it')
    target.mkdir(parents=True)
    subprocess.run(['git', 'init', str(target)], check=True)
    git(target, 'remote', 'add', 'origin', entry['url'])
    git(target, 'fetch', '--depth', '1', 'origin', entry['commit'])
    git(target, 'checkout', '--detach', 'FETCH_HEAD')
    actual = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != entry['commit']:
        raise SystemExit('Pinned commit mismatch')
    for path, expected in LOCK['submodules_required'].items():
        # Nested enet belongs to moonlight-common-c; update in its parent checkout.
        if path.endswith('/enet'):
            git(target / 'moonlight-common-c/moonlight-common-c', 'submodule', 'update', '--init', '--depth', '1', 'enet')
        else:
            git(target, 'submodule', 'update', '--init', '--depth', '1', path)
        got = subprocess.check_output(['git', '-C', str(target / path), 'rev-parse', 'HEAD'], text=True).strip()
        if got != expected:
            raise SystemExit(f'Submodule {path} is {got}, expected {expected}')
    print(json.dumps({'moonlight-qt': actual,
                      'submodules': sorted(LOCK['submodules_required']),
                      'note': 'no install, no service operation, no OS package change'}, indent=2))


if __name__ == '__main__':
    main()
