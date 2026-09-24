# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import base64
import json
import os
import stat
import struct
from urllib.parse import urlencode
from .keys import KEYS, BUTTONS

class BackendError(RuntimeError):
    pass

def read_private_json(path):
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
            raise BackendError("private file must be an owned regular 0600 file")
        if st.st_size > 16384:
            raise BackendError("private file too large")
        return json.loads(os.read(fd, 16385))
    finally:
        os.close(fd)

class Kvmd:
    """Small bounded async HTTP/1.1 client over a PRIVATE Unix socket, never public HTTP.

    Only existing mouse outputs may be selected, before lease admission.
    No /reset, /set_connected, UDC or configfs operations are issued.
    A POST acknowledgement means kvmd accepted an event, NOT that USB host observed it.
    """
    def __init__(self, socket_path, headers=None, timeout=0.4):
        self.socket_path = socket_path
        self.headers = headers or {}
        self.timeout = timeout
        for key, value in self.headers.items():
            if key not in {"X-KVMD-User", "X-KVMD-Passwd", "Authorization"} or not isinstance(value, str) or "\r" in value or "\n" in value:
                raise BackendError("invalid authentication header configuration")

    async def request(self, method, path, **params):
        async def operation():
            reader, writer = await asyncio.open_unix_connection(self.socket_path, limit=16384)
            try:
                target = path + ("?" + urlencode(params) if params else "")
                headers = {"Host":"localhost", "Connection":"close", "Content-Length":"0", **self.headers}
                raw = f"{method} {target} HTTP/1.1\r\n" + "".join(f"{k}: {v}\r\n" for k,v in headers.items()) + "\r\n"
                writer.write(raw.encode("ascii"))
                await writer.drain()
                head = await reader.readuntil(b"\r\n\r\n")
                lines = head.decode("ascii").split("\r\n")
                fields = {}
                for line in lines[1:]:
                    if line:
                        k, v = line.split(":",1)
                        fields[k.lower()] = v.strip()
                if len(lines[0].split()) < 2 or lines[0].split()[1] != "200":
                    raise BackendError("kvmd HTTP request rejected")
                if "transfer-encoding" in fields:
                    if fields["transfer-encoding"].lower() != "chunked":
                        raise BackendError("unsupported HTTP transfer encoding")
                    body = bytearray()
                    while True:
                        size = int((await reader.readline()).split(b";",1)[0].strip(),16)
                        if size < 0 or len(body)+size > 16384:
                            raise BackendError("kvmd reply too large")
                        if not size:
                            break
                        body.extend(await reader.readexactly(size))
                        if await reader.readexactly(2) != b"\r\n":
                            raise BackendError("invalid HTTP chunk")
                else:
                    size = int(fields.get("content-length", "-1"))
                    if not 0 <= size <= 16384:
                        raise BackendError("kvmd reply requires bounded content-length")
                    body = await reader.readexactly(size)
                result = json.loads(body)
                if result.get("ok") is not True:
                    raise BackendError("kvmd event rejected")
                return result.get("result", {})
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except (OSError, ConnectionError):
                    pass
        try:
            return await asyncio.wait_for(operation(), self.timeout)
        except asyncio.CancelledError:
            raise
        except Exception:
            # Never expose event query strings, key contents or authentication values in logs/errors.
            raise BackendError("kvmd operation failed or timed out") from None

    async def check(self):
        state = await self.request("GET", "/hid")
        keyboard, mouse = state.get("keyboard", {}), state.get("mouse", {})
        if state.get("enabled") is not True or keyboard.get("online") is not True or mouse.get("online") is not True:
            raise BackendError("USB HID is not reported online")
        if type(mouse.get("absolute")) is not bool:
            raise BackendError("USB mouse mode is unknown")
        return state

    async def select_mouse(self, mode):
        if mode not in {"relative", "absolute"}:
            raise BackendError("unsupported mouse mode")
        absolute = mode == "absolute"
        target = "usb" if absolute else "usb_rel"
        state = await self.check()
        mouse = state["mouse"]
        outputs = mouse.get("outputs", {})
        # A single preconfigured mouse has no selectable output. Never create one.
        if mouse["absolute"] == absolute and outputs.get("active", "") in {"", target}:
            return
        if target not in outputs.get("available", []):
            raise BackendError("requested USB mouse output is unavailable")
        await self.request("POST", "/hid/set_params", mouse_output=target)
        # kvmd's HTTP acknowledgement alone is insufficient. Read back boundedly.
        for _ in range(5):
            state = await self.check()
            mouse = state["mouse"]
            if mouse["absolute"] == absolute and mouse.get("outputs", {}).get("active") == target:
                return
            await asyncio.sleep(0.02)
        raise BackendError("USB mouse output selection was not confirmed")

    async def key(self, key, down):
        await self.request("POST", "/hid/events/send_key", key=key, state="true" if down else "false", finish="false")
    async def button(self, button, down):
        await self.request("POST", "/hid/events/send_mouse_button", button=button, state="true" if down else "false")
    async def move(self, x, y):
        await self.request("POST", "/hid/events/send_mouse_relative", delta_x=x, delta_y=y)
    async def absolute(self, x, y):
        await self.request("POST", "/hid/events/send_mouse_move", to_x=x, to_y=y)
    async def wheel(self, x, y):
        await self.request("POST", "/hid/events/send_mouse_wheel", delta_x=x, delta_y=y)
    async def open_stream(self):
        stream = KvmdStream(self.socket_path, self.headers)
        await stream.open()
        return stream
    async def neutralize(self):
        """Explicit exclusive-ownership recovery after daemon SIGKILL/restart. Does not reset the gadget."""
        failed = False
        for key in sorted(set(KEYS.values())):
            try:
                await self.key(key, False)
            except BackendError:
                failed = True
        for button in BUTTONS.values():
            try:
                await self.button(button, False)
            except BackendError:
                failed = True
        if failed:
            raise BackendError("neutralization incomplete; exclusive input must remain blocked")

class KvmdStream:
    """One persistent kvmd /ws connection for low-latency input events.

    kvmd handles websocket messages strictly in order and without per-event replies, so
    events are written without waiting. flush() sends kvmd's binary ping and waits for the
    pong, proving every earlier event was dispatched before release/lease teardown.
    Event contents are never logged.
    """
    MAX_FRAME = 1 << 20
    # kvmd's OTG mouse process writes about one USB report per 4-4.5ms on T6.
    # Anything sent faster piles up in kvmd's unmerged queue and replays late. Pacing mouse
    # reports here keeps the backlog in rkmoon_hid's Queue, where adjacent motion is merged.
    # 10s continuous-drag test on T6: 6ms tail after the last input, no backlog.
    MOUSE_REPORT_INTERVAL = 0.0042

    def __init__(self, socket_path, headers=None, timeout=0.5):
        self.socket_path = socket_path
        self.headers = headers or {}
        self.timeout = timeout
        self.reader = self.writer = None
        self.task = None
        self.pongs = asyncio.Queue()
        self.alive = False
        self.mouse_free_at = 0.0
        self.keyboard_free_at = 0.0

    async def _slot(self, attr, reports):
        loop = asyncio.get_running_loop()
        now = loop.time()
        free_at = getattr(self, attr)
        if free_at > now:
            await asyncio.sleep(free_at - now)
            now = loop.time()
        setattr(self, attr, max(now, free_at) + reports * self.MOUSE_REPORT_INTERVAL)

    async def _mouse_slot(self, reports):
        await self._slot("mouse_free_at", reports)

    async def open(self):
        async def handshake():
            self.reader, self.writer = await asyncio.open_unix_connection(self.socket_path, limit=65536)
            key = base64.b64encode(os.urandom(16)).decode("ascii")
            headers = {"Host": "localhost", "Upgrade": "websocket", "Connection": "Upgrade",
                       "Sec-WebSocket-Key": key, "Sec-WebSocket-Version": "13", **self.headers}
            raw = "GET /ws?stream=0 HTTP/1.1\r\n" + "".join(f"{k}: {v}\r\n" for k, v in headers.items()) + "\r\n"
            self.writer.write(raw.encode("ascii"))
            await self.writer.drain()
            head = await self.reader.readuntil(b"\r\n\r\n")
            status = head.split(b"\r\n", 1)[0].split()
            if len(status) < 2 or status[1] != b"101":
                raise BackendError("kvmd websocket upgrade rejected")
        try:
            await asyncio.wait_for(handshake(), 1.5)
        except BackendError:
            await self.close(); raise
        except Exception:
            await self.close(); raise BackendError("kvmd websocket unavailable") from None
        self.alive = True
        self.task = asyncio.create_task(self._read_loop())
        await self.flush()

    def _frame(self, opcode, payload):
        header = bytearray([0x80 | opcode])
        n = len(payload)
        if n < 126: header.append(0x80 | n)
        elif n < 65536: header.append(0x80 | 126); header += struct.pack(">H", n)
        else: header.append(0x80 | 127); header += struct.pack(">Q", n)
        mask = os.urandom(4)
        return bytes(header) + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload))

    async def _read_loop(self):
        try:
            while True:
                b0, b1 = await self.reader.readexactly(2)
                opcode = b0 & 0x0F
                n = b1 & 0x7F
                if n == 126: n = struct.unpack(">H", await self.reader.readexactly(2))[0]
                elif n == 127: n = struct.unpack(">Q", await self.reader.readexactly(8))[0]
                if n > self.MAX_FRAME: raise BackendError("kvmd websocket frame too large")
                mask = await self.reader.readexactly(4) if b1 & 0x80 else None
                payload = await self.reader.readexactly(n)
                if mask: payload = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
                if opcode == 0x9:
                    self.writer.write(self._frame(0xA, payload))
                elif opcode == 0x8:
                    break
                elif opcode == 0x2 and payload[:1] == b"\xff":
                    self.pongs.put_nowait(None)
                # Text state broadcasts and other frames are ignored.
        except (asyncio.CancelledError, asyncio.IncompleteReadError, ConnectionError, OSError, BackendError):
            pass
        finally:
            self.alive = False
            self.pongs.put_nowait(False)

    async def _send(self, payload):
        if not self.alive or self.writer is None:
            raise BackendError("kvmd websocket closed")
        try:
            self.writer.write(self._frame(0x2, payload))
            await asyncio.wait_for(self.writer.drain(), self.timeout)
        except Exception:
            self.alive = False
            raise BackendError("kvmd websocket send failed") from None

    async def flush(self):
        while not self.pongs.empty(): self.pongs.get_nowait()
        await self._send(b"\x00")
        try:
            ok = await asyncio.wait_for(self.pongs.get(), self.timeout)
        except asyncio.TimeoutError:
            ok = False
        if ok is False:
            self.alive = False
            raise BackendError("kvmd websocket flush not confirmed")

    async def key(self, key, down):
        # The keyboard is its own USB endpoint with the same ~4ms report budget. A press and
        # release closer than that lost ~5-45% of taps on T6, so key reports are paced too.
        await self._slot("keyboard_free_at", 1)
        await self._send(bytes([1, 1 if down else 0]) + key.encode("ascii"))
    async def button(self, button, down):
        await self._mouse_slot(1)
        await self._send(bytes([2, 1 if down else 0]) + button.encode("ascii"))
    async def absolute(self, x, y):
        await self._mouse_slot(1)
        await self._send(b"\x03" + struct.pack(">hh", x, y))
    async def move_many(self, deltas):
        await self._mouse_slot(len(deltas))
        await self._send(b"\x04\x01" + b"".join(struct.pack(">bb", x, y) for x, y in deltas))
    async def move(self, x, y):
        await self.move_many([(x, y)])
    async def wheel(self, x, y):
        await self._mouse_slot(1)
        await self._send(b"\x05\x00" + struct.pack(">bb", x, y))

    async def close(self):
        self.alive = False
        if self.task:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
            self.task = None
        if self.writer:
            try:
                self.writer.write(self._frame(0x8, b"\x03\xe8"))
                self.writer.close()
                await asyncio.wait_for(self.writer.wait_closed(), 0.3)
            except Exception:
                pass
            self.writer = None
