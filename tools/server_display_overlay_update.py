#!/usr/bin/env python3
"""Verify the exact pre-display overlay before updating an isolated build copy."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
repo = root/'vendor/sunshine'
spec = importlib.util.spec_from_file_location('overlay', root/'tools/apply_sunshine.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)
assert overlay.git(repo, 'rev-parse', 'HEAD') == overlay.PIN
original = {name: subprocess.check_output(['git', '-C', str(repo), 'show', overlay.PIN+':'+name]).decode() for name in overlay.FILES}
new = overlay.make_changes(original)
old = dict(new)
for insertion in ('#include "src/rkmoon/rkmoon_display.hpp"\n', '    rkmoon_sunshine::put_display_info(tree);\n'):
    assert old['src/nvhttp.cpp'].count(insertion) == 1
    old['src/nvhttp.cpp'] = old['src/nvhttp.cpp'].replace(insertion, '', 1)
modified = set(subprocess.check_output(['git','-C',str(repo),'diff','--name-only','HEAD']).decode().splitlines())
assert modified == set(overlay.FILES), 'unexpected tracked modifications'
for name, expected in old.items():
    assert (repo/name).read_bytes() == expected.encode(), 'unknown edit: '+name
payload = {}
for file in (root/'sunshine').iterdir():
    if file.is_file() and file.name != 'rkmoon_display.hpp':
        payload[file.name] = file
for name in ('core.cpp', 'annexb.cpp', 'hid_client.cpp'):
    payload[name] = root/'src'/name
payload['hdmi-apps.json'] = root/'config/hdmi-apps.json'
payload['LICENSE'] = root/'LICENSE'
for file in (root/'include').rglob('*'):
    if file.is_file():
        payload['include/'+str(file.relative_to(root/'include'))] = file
actual = {str(p.relative_to(repo/'src/rkmoon')) for p in (repo/'src/rkmoon').rglob('*') if p.is_file()}
assert actual == set(payload), 'unknown or missing payload file'
for name, source in payload.items():
    assert (repo/'src/rkmoon'/name).read_bytes() == source.read_bytes(), 'unknown payload edit: '+name
untracked = subprocess.check_output(['git','-C',str(repo),'ls-files','--others','--exclude-standard']).decode().splitlines()
assert set(untracked) == {'src/rkmoon/'+name for name in payload}, 'unknown untracked file'
print('exact_previous_overlay_and_payload=pass', flush=True)
for name, content in new.items():
    (repo/name).write_text(content)
shutil.copy2(root/'sunshine/rkmoon_display.hpp', repo/'src/rkmoon/rkmoon_display.hpp')
digest = hashlib.sha256(subprocess.check_output(['git','-C',str(repo),'diff','HEAD'])).hexdigest()
print('tracked_overlay_sha256='+digest)
