#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private local PIN approval. PIN is read from terminal, never argv/logged."""
import argparse
import getpass
import os
from pathlib import Path
import re
import socket
import stat


def exchange(state, command):
    state=Path(state)
    s=state.lstat()
    if not stat.S_ISDIR(s.st_mode) or s.st_uid!=os.getuid() or s.st_mode&0o077:
        raise ValueError('private state directory (0700, owned by user) required')
    sock=state/'admin.sock'
    ss=sock.lstat()
    if not stat.S_ISSOCK(ss.st_mode) or ss.st_uid!=os.getuid() or ss.st_mode&0o077:
        raise ValueError('private admin socket (0600, owned by user) required')
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(45)
        client.connect(str(sock))
        client.sendall(command.encode('ascii'))
        response=client.recv(4096).decode('ascii')
    return response


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--state',type=Path,required=True)
    p.add_argument('action',choices=['list','pin'])
    p.add_argument('--id',help='32-hex pairing ID returned by list')
    p.add_argument('--name',default='KVM client',help='Local client label')
    a=p.parse_args()
    if a.action=='list':
        print(exchange(a.state,'LIST\n'),end='')
    else:
        if not a.id or not re.fullmatch('[0-9a-fA-F]{32}',a.id) or not 1<=len(a.name)<=128 or any(c in a.name for c in '\r\n'):
            p.error('invalid pairing ID/name')
        pin=getpass.getpass('Moonlight PIN: ')
        if not re.fullmatch('[0-9]{4}',pin):
            p.error('PIN must be four digits')
        print(exchange(a.state,f'PIN {a.id.lower()} {pin} {a.name}\n'),end='')


if __name__=='__main__':
    main()
