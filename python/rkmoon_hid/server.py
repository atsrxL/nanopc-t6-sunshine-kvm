# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import json
import logging
import os
import socket
import stat
import struct
import time
from pathlib import Path
from .lease import Lease, Queue, InputError, parse_event

LOG=logging.getLogger("rkmoon-hid")

class Server:
    def __init__(self, backend, uid=None, timeout=2.0):
        self.backend=backend;self.uid=os.getuid() if uid is None else uid;self.timeout=timeout
        self.busy=False;self.active_writer=None;self.handlers=set();self.stopping=False
        self.last_highwater=0;self.recovery=None;self.server=None
    async def reply(self, writer, ok):
        writer.write(b'{"ok":true}\n' if ok else b'{"ok":false}\n')
        await asyncio.wait_for(writer.drain(),0.2)
    async def handle(self, reader, writer):
        task=asyncio.current_task();self.handlers.add(task)
        lease=None;owns=False;workers=[]
        try:
            credentials=writer.get_extra_info("socket").getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12)
            _,uid,_=struct.unpack("3i",credentials)
            if uid!=self.uid or self.busy or self.stopping:
                await self.reply(writer,False);return
            # Set before any await: a simultaneous connection can never acquire the same lease.
            self.busy=True;owns=True;self.active_writer=writer
            hello=await asyncio.wait_for(reader.readline(),self.timeout)
            if len(hello)>512:
                raise InputError("invalid local lease handshake")
            hello=json.loads(hello)
            if not isinstance(hello,dict):raise InputError("invalid local lease handshake")
            mode=hello.get("mouse_mode","relative")
            expected={"op":"hello","version":1}
            if "mouse_mode" in hello:expected["mouse_mode"]=mode
            if hello!=expected or type(hello.get("version")) is not int or mode not in {"relative","absolute"}:
                raise InputError("invalid local lease handshake")
            # The prior owner has released all tracked state before busy can clear.
            # Select before acknowledging: even the first button/wheel uses this output.
            await asyncio.wait_for(self.backend.select_mouse(mode),1.5)
            lease=Lease(self.backend,mode);queue=Queue(64);last=time.monotonic()
            await self.reply(writer,True)
            async def read_events():
                nonlocal last
                while True:
                    line=await reader.readline()
                    if not line:return
                    if len(line)>512:raise InputError("oversize input line")
                    e=parse_event(json.loads(line));last=time.monotonic()
                    if e["op"]=="bye":return
                    if e["op"]!="ping":queue.put(e)
            async def consume():
                while True:
                    await lease.apply(await queue.pop())
            async def watchdog():
                while True:
                    await asyncio.sleep(min(0.2,self.timeout/4))
                    if time.monotonic()-last>self.timeout:raise InputError("input heartbeat expired")
            workers=[asyncio.create_task(fn()) for fn in (read_events,consume,watchdog)]
            done,_=await asyncio.wait(workers,return_when=asyncio.FIRST_COMPLETED)
            for finished in done:
                finished.result()
            self.last_highwater=queue.highwater
        except asyncio.CancelledError:
            pass
        except Exception:
            LOG.warning("input lease revoked (protocol, timeout, backend, or permission failure)")
        finally:
            for w in workers:w.cancel()
            await asyncio.gather(*workers,return_exceptions=True)
            writer.close()
            try:await writer.wait_closed()
            except (OSError,ConnectionError):pass
            if owns:
                self.active_writer=None
                if lease is not None and not await lease.release():
                    LOG.error("HID release incomplete; blocking new leases and retrying")
                    self.recovery=asyncio.create_task(self.retry_release(lease))
                else:
                    self.busy=False
            self.handlers.discard(task)
    async def retry_release(self,lease):
        while not self.stopping:
            await asyncio.sleep(0.5)
            if await lease.release():
                self.busy=False;return
        # ExecStopPost's independently invoked neutralize handles abnormal/unfinished termination.
        await lease.release()
    async def listen(self,path):
        p=Path(path);parent=p.parent
        parent.mkdir(mode=0o700,parents=True,exist_ok=True)
        st=parent.stat()
        if st.st_uid!=os.getuid() or st.st_mode&0o077:
            raise InputError("HID socket directory must be private and owned (0700)")
        if p.exists() or p.is_symlink():
            raise InputError("socket path already exists; never unlink a possible live owner automatically")
        old=os.umask(0o077)
        try:
            self.server=await asyncio.start_unix_server(self.handle,path,limit=1024)
        finally:os.umask(old)
        for sock in self.server.sockets:sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4096)
        return self.server
    async def close(self):
        self.stopping=True
        if self.server:
            self.server.close();await self.server.wait_closed()
        for handler in list(self.handlers):handler.cancel()
        await asyncio.gather(*list(self.handlers),return_exceptions=True)
        if self.recovery:
            await self.recovery
