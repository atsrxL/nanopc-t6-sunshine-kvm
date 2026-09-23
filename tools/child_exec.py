#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Give a supervised child a Linux parent-death SIGTERM, then exec without a shell."""
import ctypes
import os
import signal
import sys
if len(sys.argv)<4 or sys.argv[2]!='--':raise SystemExit('child_exec PARENT_PID -- ABS_EXEC [args]')
parent=int(sys.argv[1]);command=sys.argv[3:]
if not os.path.isabs(command[0]):raise SystemExit('absolute executable required')
libc=ctypes.CDLL(None,use_errno=True)
if libc.prctl(1,signal.SIGTERM,0,0,0)!=0 or os.getppid()!=parent:raise SystemExit('parent lost or PDEATHSIG failed')
os.execv(command[0],command)
