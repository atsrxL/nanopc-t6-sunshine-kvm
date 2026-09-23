# SPDX-License-Identifier: GPL-3.0-or-later
"""Local Unix-socket client checks, no hardware and no pairing material on disk."""
import importlib.util
import os
from pathlib import Path
import socket
import tempfile
import time
import threading
import unittest

spec=importlib.util.spec_from_file_location('admin',Path(__file__).resolve().parents[1]/'tools/admin.py')
admin=importlib.util.module_from_spec(spec);spec.loader.exec_module(admin)

class AdminTests(unittest.TestCase):
    def test_private_socket_roundtrip(self):
        with tempfile.TemporaryDirectory() as root:
            state=Path(root)/'state';state.mkdir(mode=0o700)
            path=state/'admin.sock'
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path));path.chmod(0o600);server.listen(1)
                received=[]
                def serve():
                    conn,_=server.accept()
                    with conn:
                        received.append(conn.recv(512))
                        conn.sendall(b'.\n')
                worker=threading.Thread(target=serve);worker.start()
                self.assertEqual(admin.exchange(state,'LIST\n'),'.\n')
                worker.join(timeout=2)
                self.assertEqual(received,[b'LIST\n'])
    def test_fragmented_reply_and_fragmented_peer_read(self):
        with tempfile.TemporaryDirectory() as root:
            state=Path(root)/'state';state.mkdir(mode=0o700)
            path=state/'admin.sock'
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path));path.chmod(0o600);server.listen(1)
                received=[]
                def serve():
                    conn,_=server.accept()
                    with conn:
                        while True:
                            byte=conn.recv(1)
                            if not byte:break
                            received.append(byte)
                            if byte==b'\n':break
                        for part in (b'1234567890abcdef',b'1234567890abcdef\n',b'.',b'\n'):
                            conn.sendall(part)
                            time.sleep(.001)
                worker=threading.Thread(target=serve);worker.start()
                self.assertEqual(admin.exchange(state,'LIST\n'),'1234567890abcdef1234567890abcdef\n.\n')
                worker.join(timeout=2)
                self.assertEqual(b''.join(received),b'LIST\n')
    def test_truncated_reply_refused(self):
        with tempfile.TemporaryDirectory() as root:
            state=Path(root)/'state';state.mkdir(mode=0o700)
            path=state/'admin.sock'
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path));path.chmod(0o600);server.listen(1)
                def serve():
                    conn,_=server.accept()
                    with conn:
                        conn.recv(128)
                        conn.sendall(b'NO')
                worker=threading.Thread(target=serve);worker.start()
                with self.assertRaises(ConnectionError):admin.exchange(state,'LIST\n')
                worker.join(timeout=2)
    def test_public_socket_refused(self):
        with tempfile.TemporaryDirectory() as root:
            state=Path(root)/'state';state.mkdir(mode=0o700)
            path=state/'admin.sock'
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(path));path.chmod(0o666)
                with self.assertRaises(ValueError):admin.exchange(state,'LIST\n')
    def test_public_state_refused(self):
        with tempfile.TemporaryDirectory() as root:
            state=Path(root)/'state';state.mkdir(mode=0o755)
            with self.assertRaises(ValueError):admin.exchange(state,'LIST\n')
