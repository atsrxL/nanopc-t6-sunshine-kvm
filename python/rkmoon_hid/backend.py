# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import json
import os
import stat
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
