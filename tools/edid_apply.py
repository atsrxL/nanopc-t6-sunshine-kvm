#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install or read the RK3588 HDMI-RX EDID through V4L2 VIDIOC_S_EDID / G_EDID.

The receiver forgets its EDID on every T6 boot, so rkmoon-edid.service applies it at boot.
It writes only when the current EDID differs (a write makes the source re-read its modes)
and verifies the readback byte for byte. The first differing EDID is backed up once.
"""
import argparse, ctypes, fcntl, hashlib, os, pathlib, struct, sys

G_EDID, S_EDID = 0xc0285628, 0xc0285629
BLOCKS = 2


def transfer(fd, request, raw=None):
    size = 128 * BLOCKS
    buf = ctypes.create_string_buffer(raw, size) if raw is not None else ctypes.create_string_buffer(size)
    arg = bytearray(struct.pack('=8IQ', 0, 0, BLOCKS, 0, 0, 0, 0, 0, ctypes.addressof(buf)))
    fcntl.ioctl(fd, request, arg, True)
    return buf.raw


def valid(raw):
    return len(raw) == 128 * BLOCKS and raw[:8] == bytes.fromhex('00ffffffffffff00') and \
        all(sum(raw[i:i + 128]) % 256 == 0 for i in range(0, len(raw), 128))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['apply', 'show'])
    p.add_argument('--device', default='/dev/video0')
    p.add_argument('--edid', type=pathlib.Path)
    p.add_argument('--backup', type=pathlib.Path)
    a = p.parse_args()
    if a.action == 'apply' and not a.edid:
        p.error('apply requires --edid')
    fd = os.open(a.device, os.O_RDWR | os.O_NONBLOCK)
    try:
        current = transfer(fd, G_EDID)
        if a.action == 'show':
            print('current_sha256', hashlib.sha256(current).hexdigest())
            return 0
        wanted = a.edid.read_bytes()
        if not valid(wanted):
            sys.exit('refusing invalid EDID file (size/header/checksum)')
        if current == wanted:
            print('EDID already installed', hashlib.sha256(wanted).hexdigest())
            return 0
        if a.backup and not a.backup.exists() and valid(current):
            a.backup.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            with a.backup.open('xb') as f:
                f.write(current)
        transfer(fd, S_EDID, wanted)
        if transfer(fd, G_EDID) != wanted:
            sys.exit('EDID readback mismatch')
        print('EDID installed', hashlib.sha256(wanted).hexdigest())
        return 0
    finally:
        os.close(fd)


if __name__ == '__main__':
    raise SystemExit(main())
