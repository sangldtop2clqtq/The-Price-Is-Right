"""Full gameplay over real sockets and a disposable PostgreSQL catalogue."""
import argparse
import contextlib
import math
import os
import re
import select
import struct
import subprocess
import time
import unittest
import uuid

import test_auth
from test_auth import DatabaseTests, credentials
from dev_db import sql, pg_binary
from room_client import RoomClient, string


class GameTests(DatabaseTests):
    server_env = {"PR_GAME_SINGLE_MS": "600", "PR_GAME_BASKET_MS": "900", "PR_GAME_REVEAL_MS": "200"}
    real_time = False

    def setUp(self):
        self.peers = []
        self.prices = {int(row.split("|")[0]): int(row.split("|")[1]) for row in
                       sql(self.url, "SELECT id,price_vnd FROM products").splitlines()}

    def tearDown(self):
        for peer in self.peers:
            peer.close()

    def peer(self):
        p = RoomClient("127.0.0.1", self.port)
        self.peers.append(p)
        username = "game_" + uuid.uuid4().hex[:15]
        self.assertEqual(p.call(1, credentials(username))[0], 0)
        self.assertEqual(p.call(2, credentials(username))[0], 0)
        return p

    def lobby(self, count=2):
        players = [self.peer() for _ in range(count)]
        host = players[0]
        self.assertEqual(host.call(0x101)[0], 0)
        code = host.view["code"]
        for p in players[1:]:
            self.assertEqual(p.call(0x102, string(code))[0], 0)
        for p in players:
            self.assertEqual(p.call(0x104, b"\1")[0], 0)
        return players, code

    def start(self, players):
        status, body = players[0].call(0x201)
        self.assertEqual(status, 0)
        match_id, = struct.unpack("!Q", body)
        self.assertGreater(match_id, 0)
        return match_id

    def guess(self, peer, match_id, round_no, value):
        return peer.call(0x202, struct.pack("!QBQ", match_id, round_no, value))

    def test_full_five_rounds_scoring_rankings_and_waiting(self):
        players, code = self.lobby(4)
        spectator = self.peer()
        match_id = self.start(players)
        for p in players:
            started = p.wait_game(0x8201)
            self.assertEqual((started["match_id"], started["rounds"]), (match_id, 5))
            self.assertEqual([row["id"] for row in started["participants"]], [p.user_id for p in players])
        totals = dict.fromkeys([p.user_id for p in players], 0)
        game_room_revision = 0
        for round_no in range(1, 6):
            views = [p.wait_game(0x8202, lambda v: v["round_no"] == round_no) for p in players]
            self.assertTrue(all(view == views[0] for view in views))
            view = views[0]
            expected_ms = (20000 if round_no <= 3 else 30000) if self.real_time else (600 if round_no <= 3 else 900)
            self.assertEqual(view["duration_ms"], expected_ms)
            self.assertEqual(view["type"], 0 if round_no <= 3 else 1)
            self.assertEqual(len(view["items"]), 1 if round_no <= 3 else round_no - 2)
            self.assertNotIn("actual_total", view)
            actual = sum(self.prices[item["id"]] * item["quantity"] for item in view["items"])
            if round_no == 1:
                game_room_revision = players[0].view["revision"]
                self.assertEqual(spectator.call(0x102, string(code))[0], 0)
                snapshot = spectator.wait_game(0x8205)
                self.assertEqual(snapshot["match_id"], match_id)
                self.assertEqual(snapshot["has_result"], 0)
                self.assertNotIn("actual_total", snapshot)
                self.assertEqual(spectator.view["role"], 1)
            self.assertEqual(self.guess(players[0], match_id, round_no, actual), (0, b"\1"))
            self.assertEqual(self.guess(players[1], match_id, round_no, actual // 2)[0], 0)
            self.assertEqual(self.guess(players[3], match_id, round_no, min(10**12, actual * 2))[0], 0)
            results = [p.wait_game(0x8203, lambda v: v["round_no"] == round_no) for p in [*players, spectator]]
            self.assertTrue(all(v == results[0] for v in results))
            result = results[0]
            self.assertEqual(result["actual_total"], actual)
            for entry in result["scores"]:
                if entry["submitted"]:
                    expected = math.floor(1000 * max(0, 1 - abs(entry["guess"] - actual) / actual) *
                                          (1 - entry["response_ms"] / (10 * expected_ms)) + 0.5)
                    self.assertLessEqual(abs(entry["score"] - expected), 1)
                else:
                    self.assertNotIn("guess", entry)
                    self.assertEqual(entry["score"], 0)
                totals[entry["id"]] += entry["score"]
                self.assertEqual(entry["total"], totals[entry["id"]])
            if self.real_time:
                print(f"real-time round {round_no}/5 verified ({expected_ms}ms)", flush=True)
        finals = [p.wait_game(0x8204) for p in [*players, spectator]]
        self.assertTrue(all(v == finals[0] for v in finals))
        self.assertEqual((finals[0]["state"], finals[0]["abort_reason"]), (2, 0))
        for rank in finals[0]["rankings"]:
            self.assertEqual(rank["total"], totals[rank["id"]])
            self.assertEqual(rank["rank"], 1 + sum(value > rank["total"] for value in totals.values()))
        for p in [*players, spectator]:
            view = p.update(lambda v: v["state"] == 0 and v["revision"] > game_room_revision)
            self.assertTrue(all(not row["ready"] for row in view["players"]))
        self.assertEqual(players[0].call(0x201)[0], 10)

    def test_updates_late_stale_and_spectator_guesses(self):
        players, code = self.lobby()
        watcher = self.peer()
        match_id = self.start(players)
        current = players[0].wait_game(0x8202)
        self.assertEqual(watcher.call(0x102, string(code))[0], 0)
        self.assertEqual(watcher.view["role"], 1)  # even with only two PLAYERs
        snapshot = watcher.wait_game(0x8205)
        self.assertEqual((snapshot["phase"], snapshot["has_result"]), (1, 0))
        self.assertEqual(self.guess(players[0], match_id, 1, 3000)[0], 0)
        time.sleep(0.025)
        self.assertEqual(self.guess(players[0], match_id, 1, 4000)[0], 0)
        self.assertEqual(self.guess(players[0], match_id, 1, 10**12 + 1)[0], 1)
        self.assertEqual(self.guess(watcher, match_id, 1, 0)[0], 10)
        self.assertEqual(watcher.call(0x104, b"\1")[0], 10)
        self.assertEqual(self.guess(players[1], match_id + 1, 1, 0)[0], 10)
        self.assertEqual(self.guess(players[1], match_id, 2, 0)[0], 10)
        self.assertEqual(self.guess(players[1], match_id, 1, 10**12 + 1)[0], 1)
        result = players[0].wait_game(0x8203)
        latest = next(row for row in result["scores"] if row["id"] == players[0].user_id)
        self.assertEqual(latest["guess"], 4000)
        self.assertGreaterEqual(latest["response_ms"], 25)
        self.assertEqual(self.guess(players[0], match_id, 1, 0)[0], 11)
        second = players[0].wait_game(0x8202, lambda v: v["round_no"] == 2)
        self.assertEqual(self.guess(players[0], match_id, 1, 0)[0], 10)
        self.assertNotEqual(current["round_no"], second["round_no"])

    def test_disconnect_keeps_guess_then_aborts_and_promotes_fifo(self):
        players, code = self.lobby(3)
        watchers = [self.peer() for _ in range(2)]
        match_id = self.start(players)
        players[0].wait_game(0x8202)
        for w in watchers:
            self.assertEqual(w.call(0x102, string(code))[0], 0)
        self.assertEqual(self.guess(players[0], match_id, 1, 100000)[0], 0)
        old_id = players[0].user_id
        players[0].close()
        result = players[1].wait_game(0x8203)
        old = next(row for row in result["scores"] if row["id"] == old_id)
        self.assertEqual((old["submitted"], old["guess"]), (1, 100000))
        self.assertEqual(players[1].call(0x103)[0], 0)
        for w in [players[2], *watchers]:
            over = w.wait_game(0x8204)
            self.assertEqual((over["state"], over["abort_reason"]), (3, 1))
            self.assertTrue(all(row["rank"] == 0 for row in over["rankings"]))
            room = w.update(lambda v: v["state"] == 0 and v["host"] == players[2].user_id)
            self.assertEqual(room["host"], players[2].user_id)
            self.assertEqual(room["role"], 0)
            self.assertTrue(all(not row["ready"] for row in room["players"]))

    def test_reveal_snapshot_and_reconnect_is_spectator(self):
        players, code = self.lobby(3)
        late = self.peer()
        self.start(players)
        result = players[0].wait_game(0x8203)
        self.assertEqual(late.call(0x102, string(code))[0], 0)
        snapshot = late.wait_game(0x8205)
        self.assertEqual(snapshot["phase"], 2)
        self.assertEqual(snapshot["result"], result)
        self.assertEqual(players[1].call(0x103)[0], 0)
        self.assertEqual(players[1].call(0x102, string(code))[0], 0)
        self.assertEqual(players[1].view["role"], 1)
        snapshot = players[1].wait_game(0x8205)
        self.assertEqual(next(p for p in snapshot["participants"] if p["id"] == players[1].user_id)["connected"], 0)

    def test_bad_catalogue_aborts_preparing_and_allows_new_match(self):
        players, _ = self.lobby()
        sql(self.url, "UPDATE products SET active=false")
        try:
            self.start(players)
            over = players[0].wait_game(0x8204)
            self.assertEqual((over["state"], over["abort_reason"]), (3, 2))
            players[0].update(lambda v: v["state"] == 0 and not any(p["ready"] for p in v["players"]) and
                              v["revision"] > 4)
        finally:
            sql(self.url, "UPDATE products SET active=true")
        for p in players:
            self.assertEqual(p.call(0x104, b"\1")[0], 0)
        self.start(players)
        self.assertEqual(players[0].wait_game(0x8202)["round_no"], 1)

    def test_two_rooms_finish_independently(self):
        first, _ = self.lobby()
        second, _ = self.lobby()
        a, b = self.start(first), self.start(second)
        self.assertNotEqual(a, b)
        for round_no in range(1, 6):
            for players, match in ((first, a), (second, b)):
                result = players[0].wait_game(0x8203, lambda v: v["round_no"] == round_no)
                self.assertEqual(result["match_id"], match)
        for players in (first, second):
            over = players[0].wait_game(0x8204)
            self.assertTrue(all(row["total"] == 0 and row["rank"] == 1 for row in over["rankings"]))

    def test_slow_prepare_does_not_block_running_room(self):
        active, _ = self.lobby()
        waiting, _ = self.lobby()
        self.start(active)
        active[0].wait_game(0x8202)
        blocker = subprocess.Popen([pg_binary("psql"), "-X", "-d", self.url, "-c",
            "BEGIN; SET application_name='game_product_lock'; LOCK TABLE products IN ACCESS EXCLUSIVE MODE; SELECT pg_sleep(30); COMMIT;"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                held = sql(self.url, "SELECT count(*) FROM pg_locks l JOIN pg_stat_activity a ON l.pid=a.pid "
                           "WHERE a.application_name='game_product_lock' AND l.mode='AccessExclusiveLock' AND l.granted").strip()
                if held != "0":
                    break
                time.sleep(0.01)
            self.assertNotEqual(held, "0")
            self.start(waiting)
            self.assertEqual(active[0].call(0xf0, struct.pack("!I", 7)), (0, struct.pack("!I", 7)))
            self.assertEqual(active[0].wait_game(0x8203)["round_no"], 1)
        finally:
            sql(self.url, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='game_product_lock'")
            blocker.communicate(timeout=5)
        self.assertEqual(waiting[0].wait_game(0x8202)["round_no"], 1)

    def test_cli_receives_events_submits_and_displays_final(self):
        guest = self.peer()
        name = "cli_game_" + uuid.uuid4().hex[:12]
        process = subprocess.Popen([str(test_auth.BUILD / "pr_cli"), "127.0.0.1", str(self.port), "--interactive"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output = bytearray()
        def send(text):
            process.stdin.write(text.encode()); process.stdin.flush()
        def until(token, timeout=10):
            deadline = time.monotonic() + timeout
            while token not in output:
                if time.monotonic() >= deadline:
                    self.fail("CLI output timeout: " + output.decode(errors="replace"))
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    data = os.read(process.stdout.fileno(), 8192)
                    if not data:
                        self.fail("CLI closed: " + output.decode(errors="replace"))
                    output.extend(data)
        try:
            send(f"register {name}\npassword123\nlogin {name}\npassword123\ncreate\n")
            until(b"CREATE_ROOM code=")
            while not re.search(rb"CREATE_ROOM code=([A-Z0-9]{6})", output):
                output.extend(os.read(process.stdout.fileno(), 8192))
            code = re.search(rb"CREATE_ROOM code=([A-Z0-9]{6})", output).group(1).decode()
            self.assertEqual(guest.call(0x102, string(code))[0], 0)
            self.assertEqual(guest.call(0x104, b"\1")[0], 0)
            send("ready 1\nstart\n")
            until(b"NEW_ROUND")
            send("guess 3000\nguess 4000\n")
            until(b"accepted=1 guess=3000")
            until(b"accepted=1 guess=4000")
            until(b"guess=4000 response_ms=")
            self.assertIn(b"guess=4000 response_ms=", output)
            until(b"GAME_OVER", timeout=15)
            send("quit\n")
            rest, error = process.communicate(timeout=5)
            output.extend(rest)
            self.assertEqual(process.returncode, 0, error.decode())
            self.assertIn(b"outcome=FINISHED", output)
            self.assertEqual(output.count(b"ROUND_RESULT match_id="), 5)
        finally:
            if process.poll() is None:
                process.terminate(); process.communicate(timeout=5)

    def test_automatic_game_demo(self):
        prefix = "gd_" + uuid.uuid4().hex[:10]
        result = subprocess.run([os.sys.executable, str(test_auth.ROOT / "tools/demo_game.py"),
            "--port", str(self.port), "--account-prefix", prefix,
            "--accounts-file", str(self.temp / "game_demo.json"), "--skip-last"],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS GAME_OVER", result.stdout)
        self.assertEqual(result.stdout.count("RESULT "), 5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--real-time", action="store_true")
    args = parser.parse_args()
    test_auth.BUILD = (test_auth.ROOT / args.build_dir).resolve()
    if args.real_time:
        GameTests.server_env = {"PR_GAME_SINGLE_MS": "20000", "PR_GAME_BASKET_MS": "30000", "PR_GAME_REVEAL_MS": "5000"}
        GameTests.real_time = True
        suite = unittest.TestSuite([GameTests("test_full_five_rounds_scoring_rankings_and_waiting")])
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        raise SystemExit(not result.wasSuccessful())
    unittest.main(argv=[os.path.basename(__file__)], verbosity=2)
