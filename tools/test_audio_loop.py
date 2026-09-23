#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link a synthetic audio-loop fixture from a completed Ninja Sunshine build.
No real audio device/video/USB is opened. Uses ALSA null + injected xrun/ring status.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    root=Path(__file__).resolve().parents[1]
    entries=json.loads((build/'compile_commands.json').read_text())
    entries=[e for e in entries if e['file'].endswith('/src/rkmoon/rkmoon_main.cpp')]
    if len(entries)!=1:raise SystemExit('expected exactly one dedicated main compile command')
    e=entries[0];cmd=e.get('arguments') or shlex.split(e['command'])
    original_object=cmd[cmd.index('-o')+1]
    obj=out/'sunshine-audio-fixture.o';binary=out/'sunshine-audio-fixture'
    cmd[cmd.index('-o')+1]=str(obj)
    cmd[cmd.index('-c')+1]=str(root/'tests/sunshine_audio_fixture.cpp')
    subprocess.run(cmd,cwd=build,check=True)
    commands=subprocess.check_output(['ninja','-t','commands','sunshine'],cwd=build,text=True).splitlines()
    link=[line for line in commands if ' -o rkmoon-kvm ' in line]
    if len(link)!=1:raise SystemExit('expected exactly one dedicated host link command')
    args=shlex.split(link[0])
    if args[:2]!=[':','&&'] or args[-2:]!=['&&',':']:raise SystemExit('unknown Ninja link command wrapper')
    args=args[2:-2]
    if args.count(original_object)!=1:raise SystemExit('main object must appear exactly once')
    args[args.index(original_object)]=str(obj)
    args[args.index('-o')+1]=str(binary)
    args.insert(1,'-Wl,--wrap=snd_pcm_avail_update')
    subprocess.run(args,cwd=build,check=True)
    subprocess.run([str(binary),str(out/'audio-loop-internal.log')],cwd=out,check=True,timeout=45)

if __name__=='__main__':main()
