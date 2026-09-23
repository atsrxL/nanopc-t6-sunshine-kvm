#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ExecStopPost recovery. Disabled input is a true no-op, not a HID request."""
import argparse
import fcntl
import os
from pathlib import Path
import subprocess
from run import load, private_state, hid_command, base_env
p=argparse.ArgumentParser();p.add_argument('--config',type=Path,required=True);a=p.parse_args();c=load(a.config)
if not c['input']['enabled']:raise SystemExit(0)
state=private_state(c)
# A failed second start must NEVER release an active first instance's input.
lockfd=os.open(state/'supervisor.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
try:
    try:fcntl.flock(lockfd,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise SystemExit(0)
    marker=state/'hid-recovery-needed.json'
    if not marker.exists():raise SystemExit(0) # This service did not start/own an input child.
    code=subprocess.run(hid_command(c,state,True),env=base_env(c,state),timeout=70).returncode
    if code==0:marker.unlink(missing_ok=True)
    raise SystemExit(code)
finally:os.close(lockfd)
