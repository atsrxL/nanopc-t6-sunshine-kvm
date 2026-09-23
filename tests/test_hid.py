# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline fixtures: fake kvmd and real local Unix sockets, NOT a USB hardware test."""
import asyncio
import json
import os
from pathlib import Path
import tempfile
import unittest

from rkmoon_hid.backend import Kvmd, BackendError, read_private_json
from rkmoon_hid.lease import Lease, Queue, InputError, chunks, parse_event
from rkmoon_hid.server import Server

class Fake:
    def __init__(self): self.events=[]; self.fail_down=False; self.fail_release=False; self.absolute=False
    async def check(self):
        if self.absolute: raise BackendError('relative required')
    async def key(self,key,down):
        self.events.append(('key',key,down))
        if (down and self.fail_down) or (not down and self.fail_release): raise BackendError('fixture timeout')
    async def button(self,key,down):
        self.events.append(('button',key,down))
        if not down and self.fail_release: raise BackendError('fixture release failure')
    async def move(self,x,y): self.events.append(('move',x,y))
    async def wheel(self,x,y): self.events.append(('wheel',x,y))

class ProtocolTests(unittest.TestCase):
    def test_key(self): self.assertEqual(parse_event({'op':'key','vk':65,'down':True})['vk'],65)
    def test_integer_bool(self):
        with self.assertRaises(InputError): parse_event({'op':'move','x':True,'y':0})
    def test_unknown_key(self):
        with self.assertRaises(InputError): parse_event({'op':'key','vk':0,'down':True})
    def test_function_key_backend_contract(self):
        # Pinned kvmd rejects most F13-F24 names. They must never enter a
        # lease or startup neutralization, which otherwise fails every start.
        from rkmoon_hid.keys import KEYS
        self.assertEqual({v for v in KEYS.values() if v.startswith('F')},
                         {f'F{i}' for i in range(1,13)})
        for vk in range(0x7C,0x88):
            with self.assertRaises(InputError):
                parse_event({'op':'key','vk':vk,'down':True})
    def test_extra_field(self):
        with self.assertRaises(InputError): parse_event({'op':'ping','extra':1})
    def test_missing_field(self):
        with self.assertRaises(InputError): parse_event({'op':'key','vk':65})
    def test_numeric_state(self):
        with self.assertRaises(InputError): parse_event({'op':'key','vk':65,'down':1})
    def test_no_absolute(self):
        with self.assertRaises(InputError): parse_event({'op':'absolute','x':0,'y':0})
    def test_move_range(self):
        with self.assertRaises(InputError): parse_event({'op':'move','x':32768,'y':0})
    def test_buttons(self):
        for b in range(1,6): parse_event({'op':'button','button':b,'down':False})
    def test_chunk_sum(self):
        for x,y in [(1000,-400),(-128,128),(0,0),(-32767,32767)]:
            cc=list(chunks(x,y)); self.assertEqual((sum(a for a,b in cc),sum(b for a,b in cc)),(x,y))
            self.assertTrue(all(-127<=a<=127 and -127<=b<=127 for a,b in cc))
    def test_private_file(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'auth'; p.write_text('{}'); p.chmod(0o600)
            self.assertEqual(read_private_json(p),{})
            p.chmod(0o644)
            with self.assertRaises(BackendError): read_private_json(p)
    def test_auth_symlink(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'auth'; p.write_text('{}'); p.chmod(0o600)
            q=Path(d)/'link'; q.symlink_to(p)
            with self.assertRaises(OSError): read_private_json(q)
    def test_crlf(self):
        with self.assertRaises(BackendError): Kvmd('/not-opened',{'Authorization':'x\r\ny'})
    def test_forbidden_header(self):
        with self.assertRaises(BackendError): Kvmd('/not-opened',{'X-Other':'x'})

class LeaseTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self): self.b=Fake(); self.l=Lease(self.b)
    async def key(self,vk,down): await self.l.apply(parse_event({'op':'key','vk':vk,'down':down}))
    async def test_key_release(self):
        await self.key(65,True); self.assertTrue(await self.l.release())
        self.assertEqual(self.b.events,[('key','KeyA',True),('key','KeyA',False)])
    async def test_modifier_alias(self):
        await self.key(0x10,True); await self.key(0xA0,True); await self.key(0x10,False)
        self.assertEqual(len(self.b.events),1)
        await self.key(0xA0,False); self.assertEqual(self.b.events[-1],('key','ShiftLeft',False))
    async def test_repeat(self):
        await self.key(65,True); await self.key(65,True)
        self.assertEqual(len(self.b.events),1)
    async def test_down_timeout_still_release(self):
        self.b.fail_down=True
        with self.assertRaises(BackendError): await self.key(65,True)
        self.assertTrue(await self.l.release()); self.assertEqual(self.b.events[-1],('key','KeyA',False))
    async def test_release_retry(self):
        await self.key(65,True); self.b.fail_release=True
        self.assertFalse(await self.l.release()); self.b.fail_release=False
        self.assertTrue(await self.l.release())
    async def test_combo_release_all(self):
        for k in [0xA2,0xA4,0x2E]: await self.key(k,True)
        await self.l.release(); self.assertEqual(sum(1 for e in self.b.events if e[2] is False),3)
    async def test_button_release(self):
        await self.l.apply({'op':'button','button':1,'down':True}); await self.l.release()
        self.assertEqual(self.b.events,[('button','left',True),('button','left',False)])
    async def test_wheel_fraction(self):
        await self.l.apply({'op':'wheel','x':0,'y':-60}); self.assertFalse(self.b.events)
        await self.l.apply({'op':'wheel','x':0,'y':-60}); self.assertEqual(self.b.events,[('wheel',0,-1)])
    async def test_move_chunks(self):
        await self.l.apply({'op':'move','x':256,'y':-128})
        self.assertEqual(sum(e[1] for e in self.b.events),256); self.assertEqual(sum(e[2] for e in self.b.events),-128)
    async def test_queue_coalesce(self):
        q=Queue(2); q.put({'op':'move','x':2,'y':3});q.put({'op':'move','x':4,'y':5})
        self.assertEqual(await q.pop(),{'op':'move','x':6,'y':8});self.assertEqual(q.highwater,1)
    async def test_queue_edge(self):
        q=Queue(3);q.put({'op':'move','x':2,'y':3});q.put({'op':'key','vk':65,'down':True});q.put({'op':'move','x':4,'y':5})
        self.assertEqual(len(q.items),3)
    async def test_queue_overflow(self):
        q=Queue(1);q.put({'op':'key','vk':65,'down':True})
        with self.assertRaises(InputError): q.put({'op':'key','vk':65,'down':False})
    async def test_queue_wait(self):
        q=Queue(); t=asyncio.create_task(q.pop()); await asyncio.sleep(0)
        q.put({'op':'ping'});self.assertEqual(await t,{'op':'ping'})

async def until(pred,timeout=1.5):
    deadline=asyncio.get_running_loop().time()+timeout
    while not pred():
        if asyncio.get_running_loop().time()>deadline: raise AssertionError('fixture deadline')
        await asyncio.sleep(0.005)

class ServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.path=str(Path(self.tmp.name)/'hid.sock')
        self.b=Fake();self.s=Server(self.b,timeout=0.15);await self.s.listen(self.path);self.clients=[]
    async def asyncTearDown(self):
        for w in self.clients:
            w.close()
            try: await w.wait_closed()
            except OSError: pass
        await self.s.close();self.tmp.cleanup()
    async def client(self):
        r,w=await asyncio.open_unix_connection(self.path);self.clients.append(w)
        w.write(b'{"op":"hello","version":1}\n');await w.drain()
        return r,w,await r.readline()
    async def press(self,w):
        w.write(b'{"op":"key","vk":65,"down":true}\n');await w.drain()
        await until(lambda:('key','KeyA',True) in self.b.events)
    async def test_exclusive(self):
        _,w,reply=await self.client();self.assertEqual(reply,b'{"ok":true}\n')
        _,_,reply2=await self.client();self.assertEqual(reply2,b'{"ok":false}\n')
    async def test_eof_releases(self):
        r,w,_=await self.client();await self.press(w);w.close();await w.wait_closed()
        await until(lambda:not self.s.busy);self.assertIn(('key','KeyA',False),self.b.events)
    async def test_heartbeat_releases(self):
        r,w,_=await self.client();await self.press(w)
        self.assertEqual(await asyncio.wait_for(r.read(),1),b'')
        await until(lambda:not self.s.busy);self.assertIn(('key','KeyA',False),self.b.events)
    async def test_ping_keeps_lease(self):
        _,w,_=await self.client()
        for _ in range(5):
            w.write(b'{"op":"ping"}\n');await w.drain();await asyncio.sleep(0.05)
        self.assertTrue(self.s.busy)
    async def test_bad_json_releases(self):
        r,w,_=await self.client();await self.press(w);w.write(b'invalid\n');await w.drain()
        await until(lambda:not self.s.busy);self.assertIn(('key','KeyA',False),self.b.events)
    async def test_shutdown_releases(self):
        _,w,_=await self.client();await self.press(w);await self.s.close()
        self.assertIn(('key','KeyA',False),self.b.events)
    async def test_failed_release_latches_busy(self):
        _,w,_=await self.client();await self.press(w);self.b.fail_release=True;w.close();await w.wait_closed()
        await until(lambda:self.s.recovery is not None);self.assertTrue(self.s.busy)
        _,_,reply=await self.client();self.assertEqual(reply,b'{"ok":false}\n')
        self.b.fail_release=False;await until(lambda:not self.s.busy)
    async def test_absolute_rejected(self):
        self.b.absolute=True;r,w,reply=await self.client();self.assertNotEqual(reply,b'{"ok":true}\n')
    async def test_oversize_releases(self):
        r,w,_=await self.client();await self.press(w);w.write(b'x'*1500+b'\n');await w.drain()
        await until(lambda:not self.s.busy);self.assertIn(('key','KeyA',False),self.b.events)
    async def test_path_ownership_guard(self):
        other=Server(self.b)
        with self.assertRaises(InputError): await other.listen(self.path)

class HttpTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.path=str(Path(self.tmp.name)/'kvmd.sock')
        self.response={'ok':True,'result':{'enabled':True,'keyboard':{'online':True},'mouse':{'online':True,'absolute':False}}}
        self.chunked=False;self.code=200;self.raw=None;self.calls=[]
        async def handler(r,w):
            try:
                h=await r.readuntil(b'\r\n\r\n');self.calls.append(h)
                body=json.dumps(self.response).encode()
                if self.raw: response=self.raw
                elif self.chunked: response=f'HTTP/1.1 {self.code} OK\r\nTransfer-Encoding: chunked\r\n\r\n{len(body):x}\r\n'.encode()+body+b'\r\n0\r\n\r\n'
                else: response=f'HTTP/1.1 {self.code} OK\r\nContent-Length: {len(body)}\r\n\r\n'.encode()+body
                w.write(response);await w.drain()
            finally: w.close();await w.wait_closed()
        self.server=await asyncio.start_unix_server(handler,self.path);self.k=Kvmd(self.path)
    async def asyncTearDown(self): self.server.close();await self.server.wait_closed();self.tmp.cleanup()
    async def test_state(self): await self.k.check()
    async def test_absolute_state(self):
        self.response['result']['mouse']['absolute']=True
        with self.assertRaises(BackendError): await self.k.check()
    async def test_offline_state(self):
        self.response['result']['keyboard']['online']=False
        with self.assertRaises(BackendError): await self.k.check()
    async def test_chunked(self): self.chunked=True;await self.k.check()
    async def test_http_reject(self):
        self.code=401
        with self.assertRaises(BackendError): await self.k.check()
    async def test_json_reject(self):
        self.response['ok']=False
        with self.assertRaises(BackendError): await self.k.check()
    async def test_bounded_reply(self):
        self.raw=b'HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n'
        with self.assertRaises(BackendError): await self.k.check()
    async def test_unknown_length(self):
        self.raw=b'HTTP/1.1 200 OK\r\n\r\n{}'
        with self.assertRaises(BackendError): await self.k.check()
    async def test_event_paths(self):
        await self.k.key('KeyA',False);await self.k.move(1,-1);await self.k.wheel(0,1)
        self.assertIn(b'POST /hid/events/send_key?',self.calls[0]);self.assertIn(b'state=false',self.calls[0])
        self.assertIn(b'POST /hid/events/send_mouse_relative?',self.calls[1])
        self.assertIn(b'POST /hid/events/send_mouse_wheel?',self.calls[2])
        self.assertTrue(all(b'set_params' not in c and b'/reset' not in c for c in self.calls))

if __name__=='__main__': unittest.main()
