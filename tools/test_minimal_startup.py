#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Isolated Linux startup/port checks with a synthetic probe-only worker.
No video/audio/USB source is opened; no actual pairing or stream is accepted.
Run in a task-owned network namespace/container, unprivileged except --root-refusal.
"""
import argparse
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--root-refusal',action='store_true')
    a=p.parse_args();binary=a.binary.resolve();out=a.output.resolve()
    if a.root_refusal:
        if os.geteuid()!=0:raise SystemExit('root-refusal check needs isolated container uid0')
        r=subprocess.run([str(binary),'/unused/private.conf'],capture_output=True,text=True,timeout=5)
        assert r.returncode==1 and 'refuses root' in r.stderr
        print('isolated_root_refusal=pass no_device_open');return
    if os.geteuid()==0:raise SystemExit('run startup smoke as non-root container user')
    out.mkdir(parents=True,exist_ok=True);out.chmod(0o700)
    with tempfile.TemporaryDirectory(prefix='private-state-',dir=out) as temp:
        state=Path(temp);state.chmod(0o700)
        xdg=state/'xdg';xdg.mkdir(mode=0o700)
        config=state/'host.conf'
        config.write_text('sunshine_name = Synthetic startup test\nmin_log_level = 0\nupnp = disabled\ncontroller = disabled\n')
        config.chmod(0o600)
        worker=state/'probe-only.py'
        worker.write_text('''#!/usr/bin/python3
# SYNTHETIC IPC capability response; NOT a hardware probe.
import socket,struct,sys
if '--probe' not in sys.argv:sys.exit(4)
b=bytearray(64);b[:4]=b'RKMF'
struct.pack_into('<HH',b,4,1,7);struct.pack_into('<I',b,60,3)
socket.socket(fileno=3).sendall(b)
''');worker.chmod(0o700)
        env=dict(os.environ,HOME=str(state),XDG_CONFIG_HOME=str(xdg),RKMOON_ADMIN_SOCKET=str(state/'admin.sock'),RKMOON_WORKER=str(worker))
        for name in ['RKMOON_CAPTURE_AUTHORIZED','RKMOON_HID_SOCKET','RKMOON_AUDIO_DEVICE','CONFIGURATION_DIRECTORY','SUNSHINE_MIGRATE_CONFIG']:
            env.pop(name,None)
        # Absolute config is enforced before cwd is changed.
        r=subprocess.run([str(binary),'relative.conf'],cwd='/tmp',env=env,capture_output=True,text=True,timeout=5)
        assert r.returncode==2 and 'absolute/path' in r.stderr
        print('relative_config_refused=pass')
        missing=state/'without-assets';missing.mkdir(mode=0o700)
        copied=missing/'rkmoon-kvm';shutil.copy2(binary,copied)
        r=subprocess.run([str(copied),str(config)],cwd='/tmp',env=env,capture_output=True,text=True,timeout=5)
        assert r.returncode==1 and 'asset missing' in r.stderr
        shutil.rmtree(missing)
        print('missing_assets_refused=pass')
        log=out/'synthetic-host-startup.log'
        with log.open('w') as logfile:
            process=subprocess.Popen([str(binary),str(config)],cwd='/tmp',env=env,stdout=logfile,stderr=subprocess.STDOUT)
            try:
                deadline=time.monotonic()+30
                ports=set()
                while time.monotonic()<deadline and process.poll() is None:
                    inodes=set()
                    for fd in Path(f'/proc/{process.pid}/fd').iterdir():
                        try:
                            target=os.readlink(fd)
                            if target.startswith('socket:['):inodes.add(target[8:-1])
                        except OSError:pass
                    ports=set()
                    for table in ['tcp','tcp6']:
                        for line in Path('/proc/net/'+table).read_text().splitlines()[1:]:
                            fields=line.split()
                            if fields[3]=='0A' and fields[9] in inodes:ports.add(int(fields[1].split(':')[1],16))
                    if {47984,47989,48010}<=ports and (state/'admin.sock').exists():break
                    time.sleep(.1)
                assert process.poll() is None, 'synthetic host failed; see private startup log'
                assert ports=={47984,47989,48010}, f'unexpected startup listener set: {sorted(ports)}'
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(3);client.connect(str(state/'admin.sock'))
                    for fragment in (b'L',b'IS',b'T',b'\n'):client.sendall(fragment);time.sleep(.005)
                    data=b''
                    while not data.endswith(b'.\n'):data+=client.recv(100)
                    assert data==b'.\n'
                assert (xdg/'sunshine/apps.json').exists()
                assert (xdg/'sunshine/sunshine.log').exists()
                print('different_cwd_and_private_state=pass')
                print('synthetic_probe_startup_tcp_listeners='+','.join(map(str,sorted(ports))))
                print('web_port_47990_absent=pass startup_only_no_stream')
                print('fragmented_admin_list=pass real_socket_same_uid')
            finally:
                if process.poll() is None:
                    start=time.monotonic();process.terminate()
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill();process.wait();raise
                    print(f'synthetic_host_shutdown_ms={int((time.monotonic()-start)*1000)} exit={process.returncode}')
        assert not (state/'admin.sock').exists(), 'admin socket not released'
        print('admin_socket_cleanup=pass')
    print('synthetic_private_keys_state_deleted=true no_real_pairing_or_media')

if __name__=='__main__':main()
