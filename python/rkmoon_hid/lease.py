# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
from collections import deque
from .keys import KEYS, BUTTONS

class InputError(RuntimeError):
    pass

def integer(event, name, low=-32767, high=32767):
    value = event.get(name)
    if type(value) is not int or not low <= value <= high:
        raise InputError("invalid input integer")
    return value

def parse_event(e):
    if not isinstance(e, dict) or not isinstance(e.get("op"), str):
        raise InputError("invalid input event")
    op = e["op"]
    allowed = {"ping":{"op"}, "bye":{"op"}, "key":{"op","vk","down"},
               "button":{"op","button","down"}, "move":{"op","x","y"}, "wheel":{"op","x","y"}}
    if op not in allowed or set(e) != allowed[op]:
        raise InputError("unsupported input event/fields")
    if op in {"key","button"}:
        if type(e.get("down")) is not bool:
            raise InputError("invalid input state")
        if op == "key":
            if integer(e,"vk",0,255) not in KEYS:
                raise InputError("unsupported normalized virtual key")
        elif integer(e,"button",1,5) not in BUTTONS:
            raise InputError("unsupported mouse button")
    elif op in {"move","wheel"}:
        integer(e,"x"); integer(e,"y")
    return e

class Queue:
    def __init__(self, capacity=64):
        self.items=deque(); self.capacity=capacity; self.ready=asyncio.Event(); self.highwater=0
    def put(self, event):
        # Merge only ADJACENT motion. Never cross a key/button/wheel edge or lose a release.
        if event["op"]=="move" and self.items and self.items[-1]["op"]=="move":
            last=self.items[-1]; x,y=last["x"]+event["x"],last["y"]+event["y"]
            if abs(x)<=32767 and abs(y)<=32767:
                self.items[-1]={"op":"move","x":x,"y":y};return
        if len(self.items)>=self.capacity:
            raise InputError("input queue saturated; revoke lease")
        self.items.append(event);self.highwater=max(self.highwater,len(self.items));self.ready.set()
    async def pop(self):
        while not self.items:
            self.ready.clear(); await self.ready.wait()
        return self.items.popleft()

def chunks(x,y):
    while x or y:
        a=max(-127,min(127,x)); b=max(-127,min(127,y)); yield a,b; x-=a;y-=b

class Lease:
    def __init__(self, backend):
        self.backend=backend
        self.virtual=set() # Down VK aliases, only in memory. Never log or persist these sets.
        self.possibly_down=set(); self.buttons=set()
        self.wheel_x=self.wheel_y=0
    async def apply(self, e):
        op=e["op"]
        if op=="key":
            vk=e["vk"];key=KEYS[vk]
            if e["down"]:
                if vk in self.virtual:
                    return # USB host handles autorepeat; duplicate network down is not a release/press.
                already=any(KEYS[v]==key for v in self.virtual)
                self.virtual.add(vk)
                if not already:
                    self.possibly_down.add(key) # BEFORE request; timeout may still have applied it.
                    await self.backend.key(key,True)
            else:
                self.virtual.discard(vk)
                if not any(KEYS[v]==key for v in self.virtual) and key in self.possibly_down:
                    await self.backend.key(key,False);self.possibly_down.discard(key)
        elif op=="button":
            b=BUTTONS[e["button"]]
            if e["down"]:
                if b not in self.buttons:
                    self.buttons.add(b);await self.backend.button(b,True)
            elif b in self.buttons:
                await self.backend.button(b,False);self.buttons.discard(b)
        elif op=="move":
            for x,y in chunks(e["x"],e["y"]):
                await self.backend.move(x,y)
        elif op=="wheel":
            self.wheel_x+=e["x"];self.wheel_y+=e["y"]
            # Truncate toward zero so fractional negative scrolling is not rounded to a full notch.
            x=int(self.wheel_x/120);y=int(self.wheel_y/120)
            self.wheel_x-=x*120;self.wheel_y-=y*120
            for a,b in chunks(x,y):
                await self.backend.wheel(a,b)
    async def release(self):
        self.virtual.clear();self.wheel_x=self.wheel_y=0
        # Failed releases remain tracked and block the next lease until the retry succeeds.
        for key in tuple(self.possibly_down):
            try:
                await self.backend.key(key,False);self.possibly_down.discard(key)
            except Exception:
                pass
        for b in tuple(self.buttons):
            try:
                await self.backend.button(b,False);self.buttons.discard(b)
            except Exception:
                pass
        return not self.possibly_down and not self.buttons
