# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import asyncio
import logging
import os
import signal
from pathlib import Path
from .backend import Kvmd, read_private_json
from .server import Server

async def main(args):
    if not args.exclusive_hid_authorized:
        raise RuntimeError("explicit --exclusive-hid-authorized is required; no HID requests sent")
    headers=read_private_json(args.headers) if args.headers else {}
    backend=Kvmd(args.kvmd_socket,headers)
    if args.release_all:
        await backend.neutralize();return
    await backend.check()
    # Recover from an earlier killed bridge before accepting a new session. Exclusive ownership is required.
    await backend.neutralize()
    server=Server(backend);await server.listen(args.socket)
    stop=asyncio.Event();loop=asyncio.get_running_loop()
    for sig in (signal.SIGINT,signal.SIGTERM):loop.add_signal_handler(sig,stop.set)
    try:await stop.wait()
    finally:
        await server.close()
        Path(args.socket).unlink(missing_ok=True)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description="Exclusive Unix-socket Moonlight -> existing kvmd HID bridge; no gadget reconfiguration")
    parser.add_argument("--socket",required=True)
    parser.add_argument("--kvmd-socket",required=True)
    parser.add_argument("--headers",help="optional owned 0600 JSON authentication headers; never place in repository")
    parser.add_argument("--exclusive-hid-authorized",action="store_true")
    parser.add_argument("--release-all",action="store_true")
    args=parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    try:asyncio.run(main(args))
    except Exception:
        logging.error("HID bridge unavailable; check private configuration, permissions, relative HID state, and ownership")
        raise SystemExit(1)
