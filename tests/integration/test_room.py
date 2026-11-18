"""Real room/session/event tests using the isolated PostgreSQL fixture."""
import argparse
import concurrent.futures
import os
import socket
import struct
import subprocess
import time
import unittest
import uuid
import json

import test_auth
from test_auth import DatabaseTests, credentials
from test_transport import HEADER, exact, frame


def string(text):
    data = text.encode()
    return struct.pack("!H", len(data)) + data


def decode_room(payload):
    pos = 0
    def number(fmt):
        nonlocal pos
        size = struct.calcsize("!" + fmt)
        value, = struct.unpack_from("!" + fmt, payload, pos)
        pos += size
        return value
    def text():
        nonlocal pos
        length = number("H")
        value = payload[pos:pos + length].decode()
        pos += length
        return value
    result = dict(code=text(), state=number("B"), host=number("Q"), revision=number("I"),
                  role=number("B"), spectators=number("I"))
    result["players"] = [dict(id=number("Q"), name=text(), ready=number("B"), connected=number("B"))
                         for _ in range(number("H"))]
    assert pos == len(payload) and len(result["players"]) <= 4
    return result


class Peer:
    def __init__(self, sock):
        self.sock = sock
        self.next_id = 1
        self.events = []
        self.user_id = None

    def receive(self):
        magic, version, kind, code, length, request_id = HEADER.unpack(exact(self.sock, 12))
        assert (magic, version) == (0x5052, 1) and length <= 4096
        return kind, code, request_id, exact(self.sock, length)

    def call(self, code, payload=b""):
        request_id = self.next_id
        self.next_id += 1
        self.sock.sendall(frame(code, request_id, payload))
        while True:
            kind, received_code, received_id, body = self.receive()
            if kind == 2:
                assert (received_code, received_id) == (0x8101, 0)
                self.events.append(decode_room(body))
                continue
            assert (kind, received_code, received_id) == (1, code, request_id)
            status, = struct.unpack("!H", body[:2])
            if status:
                assert len(body) == 2
            return status, body[2:]

    def update(self, predicate=lambda view: True):
        while True:
            if self.events:
                view = self.events.pop(0)
            else:
                kind, code, request_id, body = self.receive()
                assert (kind, code, request_id) == (2, 0x8101, 0)
                view = decode_room(body)
            if predicate(view):
                return view

    def close(self):
        self.sock.close()


class RoomTests(DatabaseTests):
    def test_automatic_demo_reuses_accounts_and_checks_spectator_fifo(self):
        prefix = "demo_" + uuid.uuid4().hex[:8]
        common = ["--host", "127.0.0.1", "--port", str(self.port), "--players", "4",
                  "--account-prefix", prefix, "--accounts-file", str(self.temp / "demo_accounts.json")]
        from dev_db import sql
        before = int(sql(self.url, "SELECT count(*) FROM users").strip())
        for extra in ([], [], ["--spectators", "1", "--exercise-fifo"]):
            result = subprocess.run([os.sys.executable, str(test_auth.ROOT / "tools/demo_room.py"), *common, *extra],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS room=", result.stdout)
            if extra:
                self.assertIn("PASS FIFO", result.stdout)
        self.assertEqual(int(sql(self.url, "SELECT count(*) FROM users").strip()), before + 5)
        self.assertEqual((self.temp / "demo_accounts.json").stat().st_mode & 0o777, 0o600)
        self.assertNotIn(json.loads((self.temp / "demo_accounts.json").read_text())["password"], self.log_path.read_text())

    def test_slow_spectator_tx_limit_does_not_break_room(self):
        peers = [self.peer() for _ in range(5)]
        host, slow = peers[0], peers[4]
        code = self.create(host)["code"]
        for peer in peers[1:]:
            self.join(peer, code)
        host.update(lambda view: view["spectators"] == 1)
        slow.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        # Do not read slow.sock again. Keep the writer responsive and bound its RX batch.
        dropped = False
        for batch in range(256):
            ids = list(range(host.next_id, host.next_id + 128))
            host.next_id += 128
            host.sock.sendall(b"".join(frame(0x104, rid, bytes([(batch * 128 + i) % 2])) for i, rid in enumerate(ids)))
            received = 0
            while received < 128:
                kind, event, rid, payload = host.receive()
                if kind == 1:
                    self.assertEqual((event, rid, payload), (0x104, ids[received], b"\0\0"))
                    received += 1
                else:
                    view = decode_room(payload)
                    if view["spectators"] == 0:
                        dropped = True
            if dropped:
                break
        self.assertTrue(dropped, "slow reader must reach bounded TX limit")
        # All non-reading participants may be disconnected too; the active host stays usable.
        self.assertEqual(host.call(0xf0, struct.pack("!I", 42)), (0, struct.pack("!I", 42)))
        rows = [json.loads(line) for line in self.log_path.read_text().splitlines()]
        self.assertTrue(any(row["event"] == "disconnect" and row["reason"] == "tx_queue_limit" for row in rows))

    def setUp(self):
        self.peers = []

    def tearDown(self):
        for peer in self.peers:
            peer.close()

    def peer(self, login=True):
        peer = Peer(self.connect())
        self.peers.append(peer)
        if login:
            name = "room_" + uuid.uuid4().hex[:16]
            self.assertEqual(peer.call(1, credentials(name))[0], 0)
            status, body = peer.call(2, credentials(name))
            self.assertEqual(status, 0)
            peer.user_id, = struct.unpack("!Q", body[:8])
        return peer

    def create(self, peer):
        status, body = peer.call(0x101)
        self.assertEqual(status, 0)
        return decode_room(body)

    def join(self, peer, code):
        status, body = peer.call(0x102, string(code))
        self.assertEqual(status, 0)
        return decode_room(body)

    def test_response_before_event_and_personal_role(self):
        host, guest = self.peer(), self.peer()
        request_id = host.next_id
        host.next_id += 1
        host.sock.sendall(frame(0x101, request_id, b""))
        kind, code, rid, body = host.receive()
        self.assertEqual((kind, code, rid, body[:2]), (1, 0x101, request_id, b"\0\0"))
        created = decode_room(body[2:])
        self.assertEqual(created["host"], host.user_id)
        self.assertEqual(created["role"], 0)
        self.assertEqual(host.update()["code"], created["code"])
        joined = self.join(guest, created["code"])
        self.assertEqual(len(joined["players"]), 2)
        update = host.update(lambda v: len(v["players"]) == 2)
        self.assertEqual(update["revision"], joined["revision"])

    def test_four_players_then_spectators_ready_and_fifo(self):
        peers = [self.peer() for _ in range(6)]
        code = self.create(peers[0])["code"]
        for index, peer in enumerate(peers[1:], 1):
            view = self.join(peer, code)
            self.assertEqual(view["role"], 0 if index < 4 else 1)
        for peer in peers[:4]:
            self.assertEqual(peer.call(0x104, b"\1")[0], 0)
        ready = peers[0].update(lambda v: v["spectators"] == 2 and all(p["ready"] for p in v["players"]))
        self.assertEqual(len(ready["players"]), 4)
        self.assertEqual(peers[4].call(0x104, b"\1")[0], 10)
        self.assertEqual(peers[4].call(0x201)[0], 10)
        self.assertEqual(peers[4].call(0x202, struct.pack("!QBQ", 1, 1, 500000))[0], 10)
        self.assertEqual(peers[1].call(0x201)[0], 8)  # preserve lobby for FIFO checks
        self.assertEqual(peers[4].call(0x103)[0], 0)
        after_leave = peers[0].update(lambda v: v["revision"] > ready["revision"])
        self.assertTrue(all(p["ready"] for p in after_leave["players"]))
        self.join(peers[4], code)  # rejoin behind the sixth peer in FIFO
        self.assertEqual(peers[1].call(0x103)[0], 0)
        promoted = peers[5].update(lambda v: v["role"] == 0)
        self.assertIn(peers[5].user_id, [p["id"] for p in promoted["players"]])
        self.assertTrue(all(p["ready"] == 0 for p in promoted["players"]))
        self.assertEqual(promoted["spectators"], 1)

    def test_host_leave_disconnect_and_logout_cleanup(self):
        host, second, third = self.peer(), self.peer(), self.peer()
        code = self.create(host)["code"]
        self.join(second, code)
        self.join(third, code)
        self.assertEqual(second.call(0x201)[0], 8)
        self.assertEqual(host.call(0x103)[0], 0)
        transferred = second.update(lambda v: v["host"] == second.user_id)
        self.assertEqual(len(transferred["players"]), 2)
        second.close()
        third.update(lambda v: v["host"] == third.user_id and len(v["players"]) == 1)
        self.assertEqual(third.call(3)[0], 0)  # logout removes last member and closes room
        self.assertEqual(host.call(0x102, string(code))[0], 6)
        self.create(host)

    def test_validation_and_rejected_actions_preserve_state(self):
        anon = self.peer(False)
        for code in (0x101, 0x102, 0x103, 0x104, 0x201):
            self.assertEqual(anon.call(code)[0], 2)
        peer = self.peer()
        self.assertEqual(peer.call(0x103)[0], 10)
        self.assertEqual(peer.call(0x104, b"\1")[0], 10)
        for payload in (b"", string("abc123"), string("ABC12"), string("ABC123") + b"x", b"\x00\x06ABC\0EF"):
            self.assertEqual(peer.call(0x102, payload)[0], 1)
        view = self.create(peer)
        self.assertEqual(peer.call(0x101)[0], 10)
        self.assertEqual(peer.call(0x102, string(view["code"]))[0], 10)
        self.assertEqual(peer.call(0x101, b"x")[0], 1)
        for payload in (b"", b"\2", b"\1\0"):
            self.assertEqual(peer.call(0x104, payload)[0], 1)
        self.assertEqual(peer.call(0x201)[0], 10)
        self.assertEqual(peer.call(0x104, b"\1")[0], 0)
        current = peer.update(lambda v: v["players"][0]["ready"] == 1)
        self.assertEqual(current["revision"], view["revision"] + 1)

    def test_concurrent_joins_cap_players_at_four(self):
        peers = [self.peer() for _ in range(6)]
        code = self.create(peers[0])["code"]
        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
            views = list(pool.map(lambda peer: self.join(peer, code), peers[1:]))
        self.assertEqual(sorted(v["role"] for v in views), [0, 0, 0, 1, 1])
        view = peers[0].update(lambda v: v["spectators"] == 2)
        self.assertEqual(len({p["id"] for p in view["players"]}), 4)

    def test_rooms_do_not_receive_each_others_events(self):
        left, right = self.peer(), self.peer()
        a, b = self.create(left), self.create(right)
        self.assertNotEqual(a["code"], b["code"])
        left.update()
        right.update()
        self.assertEqual(left.call(0x104, b"\1")[0], 0)
        self.assertEqual(left.update()["code"], a["code"])
        right.sock.settimeout(0.2)
        with self.assertRaises(socket.timeout):
            right.sock.recv(1)
        right.sock.settimeout(15)

    def test_new_player_resets_ready_and_host_requires_two_players(self):
        host, guest = self.peer(), self.peer()
        code = self.create(host)["code"]
        self.assertEqual(host.call(0x104, b"\1")[0], 0)
        self.assertEqual(host.call(0x201)[0], 10)
        joined = self.join(guest, code)
        self.assertTrue(all(not p["ready"] for p in joined["players"]))
        self.assertEqual(guest.call(0x201)[0], 8)
        self.assertEqual(host.call(0x201)[0], 10)
        for peer in (host, guest):
            self.assertEqual(peer.call(0x104, b"\1")[0], 0)
        view = host.update(lambda v: all(p["ready"] for p in v["players"]))
        self.assertEqual(view["state"], 0)
        status, body = host.call(0x201)
        self.assertEqual(status, 0)
        self.assertEqual(len(body), 8)
        host.update(lambda v: v["state"] == 1)

    def test_cli_create_events_ready_leave(self):
        name = "cli_room_" + uuid.uuid4().hex[:12]
        commands = f"register {name}\npassword123\nlogin {name}\npassword123\ncreate\nready 1\nstart\nleave\nlogout\nquit\n"
        result = subprocess.run([str(test_auth.BUILD / "pr_cli"), "127.0.0.1", str(self.port), "--interactive"],
                                input=commands, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        for expected in ("CREATE_ROOM code=", "ROOM_UPDATE code=", "SET_READY OK", "WRONG_STATE(10)",
                         "LEAVE_ROOM OK", "LOGOUT OK"):
            self.assertIn(expected, result.stdout)
        self.assertNotIn("password123", result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    test_auth.BUILD = (test_auth.ROOT / args.build_dir).resolve()
    unittest.main(argv=[os.path.basename(__file__)], verbosity=2)
