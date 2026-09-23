# SPDX-License-Identifier: GPL-3.0-or-later
"""Real C++ client thread -> real Python Unix socket server -> fake HID backend. No physical USB."""
import asyncio
import os
from pathlib import Path
import tempfile
import unittest
from rkmoon_hid.server import Server
from test_hid import Fake,until
ROOT=Path(__file__).resolve().parents[1]
BINARY=Path(os.environ.get('RKMOON_NATIVE_TEST_DIR',ROOT/'build/offline'))/'rkmoon-hid-fixture'
@unittest.skipUnless(BINARY.exists(),'build the native test fixture first')
class NativeHidTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.path=str(Path(self.tmp.name)/'hid.sock');self.b=Fake()
        self.s=Server(self.b,timeout=.5);await self.s.listen(self.path)
    async def asyncTearDown(self):await self.s.close();self.tmp.cleanup()
    async def run_child(self,mode):
        proc=await asyncio.create_subprocess_exec(str(BINARY),self.path,mode)
        try:self.assertEqual(await asyncio.wait_for(proc.wait(),3),0)
        finally:
            if proc.returncode is None:proc.kill();await proc.wait()
        await until(lambda:not self.s.busy)
    async def test_real_client_heartbeat_move_and_release(self):
        await self.run_child('normal')
        self.assertIn(('key','KeyA',True),self.b.events);self.assertIn(('key','KeyA',False),self.b.events)
        moves=[e for e in self.b.events if e[0]=='move'];self.assertEqual(sum(e[1] for e in moves),256)
        self.assertIn(('wheel',0,1),self.b.events)
    async def test_real_client_abrupt_exit_releases(self):
        await self.run_child('abrupt')
        self.assertEqual(self.b.events,[('key','KeyA',True),('key','KeyA',False)])
