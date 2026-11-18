"""Durability, atomicity, private history and invitations over real TCP/PostgreSQL."""
import argparse
import contextlib
import json
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
from account_protocol import page_view, stats_view


class HistoryTests(DatabaseTests):
    server_env = {"PR_GAME_SINGLE_MS": "300", "PR_GAME_BASKET_MS": "450", "PR_GAME_REVEAL_MS": "50",
                  "PR_INVITE_TTL_MS": "200"}

    def setUp(self):
        self.peers = []

    def tearDown(self):
        for peer in self.peers:
            peer.close()

    def peer(self):
        p = RoomClient("127.0.0.1", self.port)
        self.peers.append(p)
        p.username = "hist_" + uuid.uuid4().hex[:15]
        self.assertEqual(p.call(1, credentials(p.username))[0], 0)
        self.assertEqual(p.call(2, credentials(p.username))[0], 0)
        return p

    def lobby(self, players=2):
        peers = [self.peer() for _ in range(players)]
        self.assertEqual(peers[0].call(0x101)[0], 0)
        code = peers[0].view["code"]
        for peer in peers[1:]:
            self.assertEqual(peer.call(0x102, string(code))[0], 0)
        self.ready(peers)
        return peers, code

    def ready(self, peers):
        for peer in peers:
            self.assertEqual(peer.call(0x104, b"\1")[0], 0)

    def start(self, peers):
        status, body = peers[0].call(0x201)
        self.assertEqual(status, 0)
        return struct.unpack("!Q", body)[0]

    def play(self, peers):
        match_id = self.start(peers)
        for peer in peers:
            over = peer.wait_game(0x8204)
            self.assertEqual((over["match_id"], over["state"]), (match_id, 2))
        return match_id

    def history(self, peer, cursor=0, limit=10):
        status, body = peer.call(0x301, struct.pack("!QH", cursor, limit))
        self.assertEqual(status, 0)
        return page_view(0x301, body)

    def stats(self, peer):
        status, body = peer.call(0x302)
        self.assertEqual(status, 0)
        return stats_view(body)

    def online(self, peer, cursor=0, limit=10):
        status, body = peer.call(0x105, struct.pack("!QH", cursor, limit))
        self.assertEqual(status, 0)
        return page_view(0x105, body)

    @contextlib.contextmanager
    def matches_lock(self):
        blocker = subprocess.Popen([pg_binary("psql"), "-X", "-d", self.url, "-v", "ON_ERROR_STOP=1", "-c",
            "BEGIN; SET application_name='match_save_lock'; LOCK TABLE matches IN ACCESS EXCLUSIVE MODE; SELECT pg_sleep(30); COMMIT;"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                held = sql(self.url, "SELECT count(*) FROM pg_locks l JOIN pg_stat_activity a ON l.pid=a.pid "
                           "WHERE a.application_name='match_save_lock' AND l.mode='AccessExclusiveLock' AND l.granted").strip()
                if held != "0":
                    break
                time.sleep(0.02)
            self.assertNotEqual(held, "0")
            yield
        finally:
            sql(self.url, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='match_save_lock'")
            blocker.communicate(timeout=5)

    def test_committed_snapshot_latest_guess_idempotency_and_private_stats(self):
        players, code = self.lobby()
        outsider = self.peer()
        match_id = self.start(players)
        current = players[0].wait_game(0x8202)
        product = current["items"][0]
        original = json.loads(sql(self.url, f"SELECT row_to_json(p) FROM products p WHERE id={product['id']}").strip())
        self.assertEqual(players[0].call(0x202, struct.pack("!QBQ", match_id, 1, 3000))[0], 0)
        self.assertEqual(players[0].call(0x202, struct.pack("!QBQ", match_id, 1, 4000))[0], 0)
        self.assertEqual(outsider.call(0x102, string(code))[0], 0)
        sql(self.url, f"UPDATE products SET price_vnd=price_vnd+777,name='changed after start' WHERE id={product['id']}")
        try:
            result = players[0].wait_game(0x8203)
            expected_total = sum(int(sql(self.url, f"SELECT price_vnd FROM products WHERE id={p['id']}").strip()) * p['quantity']
                                 for p in current['items']) - 777 * product['quantity']
            self.assertEqual(result["actual_total"], expected_total)
            over = players[0].wait_game(0x8204)
            self.assertEqual(over["state"], 2)
            # GAME_OVER is already a commit receipt, not a promise to save later.
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM matches WHERE id={match_id}").strip(), "1")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM rounds WHERE match_id={match_id}").strip(), "5")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM submissions WHERE match_id={match_id}").strip(), "10")
            saved_guess = sql(self.url, f"SELECT s.guessed_price_vnd FROM submissions s JOIN rounds r ON r.id=s.round_id "
                              f"WHERE s.match_id={match_id} AND r.round_no=1 AND s.user_id={players[0].user_id}").strip()
            self.assertEqual(saved_guess, "4000")
            item = sql(self.url, f"SELECT i.product_name_snapshot,i.unit_price_vnd_snapshot FROM round_items i "
                       f"JOIN rounds r ON r.id=i.round_id WHERE r.match_id={match_id} AND r.round_no=1").strip()
            self.assertEqual(item, original['name'] + "|" + str(original['price_vnd']))
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM submissions WHERE match_id={match_id} "
                                f"AND user_id={players[1].user_id} AND guessed_price_vnd IS NULL AND response_ms IS NULL").strip(), "5")
            before = sql(self.url, f"SELECT count(*) FROM round_items i JOIN rounds r ON r.id=i.round_id WHERE r.match_id={match_id}")
            for _ in range(3):
                sql(self.url, f"SELECT public.pr_save_match(snapshot) FROM matches WHERE id={match_id}")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM round_items i JOIN rounds r ON r.id=i.round_id WHERE r.match_id={match_id}"), before)
            self.assertEqual([row["match_id"] for row in self.history(players[0])["rows"]], [match_id])
            self.assertEqual(self.history(outsider)["rows"], [])
            self.assertEqual(self.stats(outsider)["finished"], 0)
            stats = self.stats(players[0])
            winner = next(row for row in over["rankings"] if row["id"] == players[0].user_id)
            self.assertEqual(stats, dict(finished=1, wins=1, average_x100=winner["total"] * 100, best=winner["total"]))
        finally:
            name = original['name'].replace("'", "''")
            sql(self.url, f"UPDATE products SET name='{name}',price_vnd={original['price_vnd']} WHERE id={product['id']}")

    def test_abort_history_pagination_does_not_count_as_finished(self):
        players, code = self.lobby()
        outsider = self.peer()
        ids = []
        for iteration in range(3):
            if iteration:
                self.assertEqual(players[1].call(0x102, string(code))[0], 0)
                self.ready(players)
            match_id = self.start(players); ids.append(match_id)
            self.assertEqual(players[1].call(0x103)[0], 0)
            over = players[0].wait_game(0x8204)
            self.assertEqual((over["state"], over["abort_reason"]), (3, 1))
        rows, cursor = [], 0
        while True:
            page = self.history(players[0], cursor, 1)
            rows += page["rows"]
            cursor = page["next_cursor"]
            if not cursor:
                break
        self.assertEqual([row["match_id"] for row in rows], list(reversed(ids)))
        self.assertTrue(all(row["state"] == 3 and row["rank"] == 0 for row in rows))
        self.assertEqual(self.stats(players[0]), dict(finished=0, wins=0, average_x100=0, best=0))
        self.assertEqual(self.history(outsider, 999999)["rows"], [])

    def test_transaction_failure_rolls_back_and_notifies_without_false_game_over(self):
        players, _ = self.lobby()
        sql(self.url, "CREATE FUNCTION reject_submission() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
                      f"IF NEW.user_id={players[0].user_id} THEN RAISE EXCEPTION 'test failure'; END IF; RETURN NEW; END $$; "
                      "CREATE TRIGGER reject_submission BEFORE INSERT ON submissions FOR EACH ROW EXECUTE FUNCTION reject_submission();")
        try:
            match_id = self.start(players)
            failed = players[0].wait_game(0x8206, timeout=15)
            self.assertEqual((failed["match_id"], failed["status"]), (match_id, 13))
            self.assertEqual(players[1].wait_game(0x8206, timeout=15), failed)
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM matches WHERE id={match_id}").strip(), "0")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM rounds WHERE match_id={match_id}").strip(), "0")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM submissions WHERE match_id={match_id}").strip(), "0")
            self.assertEqual(self.history(players[0])["rows"], [])
            self.assertEqual(self.stats(players[0])["finished"], 0)
            players[0].update(lambda v: v["state"] == 0 and v["revision"] > 4)
        finally:
            sql(self.url, "DROP TRIGGER reject_submission ON submissions; DROP FUNCTION reject_submission();")
        self.ready(players)
        self.play(players)

    def test_transient_write_failure_retries_once_and_avoids_duplicates(self):
        players, _ = self.lobby()
        sql(self.url, "CREATE SEQUENCE fail_save_once; CREATE FUNCTION fail_save_once() RETURNS trigger LANGUAGE plpgsql AS $$ "
                      "BEGIN IF nextval('fail_save_once')=1 THEN RAISE EXCEPTION 'transient'; END IF; RETURN NEW; END $$; "
                      "CREATE TRIGGER fail_save_once BEFORE INSERT ON matches FOR EACH ROW EXECUTE FUNCTION fail_save_once();")
        try:
            match_id = self.play(players)
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM matches WHERE id={match_id}").strip(), "1")
            self.assertEqual(sql(self.url, f"SELECT count(*) FROM rounds WHERE match_id={match_id}").strip(), "5")
            log = [json.loads(line) for line in self.log_path.read_text().splitlines()]
            self.assertTrue(any(row["event"] == "save_retry" and row["match_id"] == match_id for row in log))
        finally:
            sql(self.url, "DROP TRIGGER fail_save_once ON matches; DROP FUNCTION fail_save_once(); DROP SEQUENCE fail_save_once;")

    def test_save_timeout_recovers_and_never_blocks_ping(self):
        players, _ = self.lobby()
        match_id = self.start(players)
        with self.matches_lock():
            players[0].wait_game(0x8203, lambda v: v["round_no"] == 5)
            self.assertEqual(players[0].call(0xf0, struct.pack("!I", 55)), (0, struct.pack("!I", 55)))
            time.sleep(5.3)  # force one DB timeout; keep under the bounded retry window
        over = players[0].wait_game(0x8204, timeout=15)
        self.assertEqual((over["match_id"], over["state"]), (match_id, 2))
        self.assertEqual(sql(self.url, f"SELECT count(*) FROM matches WHERE id={match_id}").strip(), "1")

    def test_room_slot_reuse_does_not_apply_old_save_callback(self):
        old, _ = self.lobby()
        fresh = [self.peer(), self.peer()]
        with self.matches_lock():
            old_id = self.start(old)
            self.assertEqual(old[0].call(0x103)[0], 0)
            self.assertEqual(old[1].call(0x103)[0], 0)
            self.assertEqual(fresh[0].call(0x101)[0], 0)
            code = fresh[0].view["code"]
            self.assertEqual(fresh[1].call(0x102, string(code))[0], 0)
            self.ready(fresh)
            new_id = self.start(fresh)
            self.assertNotEqual(old_id, new_id)
        self.assertEqual(fresh[0].wait_game(0x8201)["match_id"], new_id)
        self.assertEqual(fresh[0].wait_game(0x8204)["match_id"], new_id)
        self.assertEqual(sql(self.url, f"SELECT status FROM matches WHERE id={old_id}").strip(), "3")
        self.assertEqual(sql(self.url, f"SELECT status FROM matches WHERE id={new_id}").strip(), "2")

    def test_available_players_invite_expiry_and_join(self):
        host = self.peer()
        others = [self.peer() for _ in range(4)]
        self.assertEqual(host.call(0x101)[0], 0)
        code = host.view["code"]
        rows, cursor = [], 0
        while True:
            page = self.online(host, cursor, 2)
            rows += page["rows"]; cursor = page["next_cursor"]
            if not cursor:
                break
        self.assertEqual([row["id"] for row in rows], sorted(p.user_id for p in others))
        target = others[0]
        self.assertEqual(host.call(0x106, struct.pack("!Q", target.user_id))[0], 0)
        invitation = target.wait_invite()
        self.assertEqual(invitation, dict(inviter=host.user_id, code=code, expires_ms=200))
        self.assertEqual(host.call(0x106, struct.pack("!Q", target.user_id))[0], 9)
        time.sleep(0.22)
        self.assertEqual(host.call(0x106, struct.pack("!Q", target.user_id))[0], 0)
        target.wait_invite()
        self.assertEqual(target.call(0x102, string(code))[0], 0)
        self.assertNotIn(target.user_id, [row["id"] for row in self.online(host)["rows"]])
        self.assertEqual(host.call(0x106, struct.pack("!Q", target.user_id))[0], 9)
        self.assertEqual(host.call(0x106, struct.pack("!Q", host.user_id))[0], 9)

    def test_invited_fifth_user_is_spectator_and_cannot_invite(self):
        players, code = self.lobby(4)
        watcher, idle = self.peer(), self.peer()
        self.assertEqual(players[0].call(0x106, struct.pack("!Q", watcher.user_id))[0], 0)
        watcher.wait_invite()
        self.assertEqual(watcher.call(0x102, string(code))[0], 0)
        self.assertEqual(watcher.view["role"], 1)
        self.assertEqual(watcher.call(0x106, struct.pack("!Q", idle.user_id))[0], 10)

    def test_validation_cannot_select_another_users_history(self):
        p = self.peer()
        for code in (0x301, 0x105):
            for payload in (b"", struct.pack("!QH", 0, 0), struct.pack("!QH", 0, 21), struct.pack("!QH", 0, 10)+b"x"):
                self.assertEqual(p.call(code, payload)[0], 1)
        self.assertEqual(p.call(0x302, struct.pack("!Q", 1))[0], 1)
        self.assertEqual(p.call(0x106, struct.pack("!Q", 0))[0], 1)
        self.assertEqual(p.call(0x106, struct.pack("!Q", 1))[0], 10)

    def test_cli_history_stats_players_and_invitation_accept(self):
        players, code = self.lobby()
        match_id = self.play(players)
        self.assertEqual(players[0].call(3)[0], 0)
        commands = f"login {players[0].username}\npassword123\nhistory\nstats\nplayers\nquit\n"
        result = subprocess.run([str(test_auth.BUILD / "pr_cli"), "127.0.0.1", str(self.port), "--interactive"],
                                input=commands, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"MATCH id={match_id}", result.stdout)
        self.assertIn("STATS finished=1", result.stdout)
        self.assertIn("PLAYERS count=", result.stdout)
        target = self.peer()
        self.assertEqual(target.call(3)[0], 0)
        process = subprocess.Popen([str(test_auth.BUILD / "pr_cli"), "127.0.0.1", str(self.port), "--interactive"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output = bytearray()
        def send(text):
            process.stdin.write(text.encode()); process.stdin.flush()
        def until(token):
            deadline = time.monotonic() + 5
            while token not in output:
                if time.monotonic() > deadline:
                    self.fail("CLI output timeout: " + output.decode(errors="replace"))
                ready, _, _ = select.select([process.stdout], [], [], 0.1)
                if ready:
                    chunk = os.read(process.stdout.fileno(), 8192)
                    if not chunk:
                        self.fail("CLI closed unexpectedly")
                    output.extend(chunk)
        try:
            send(f"login {target.username}\npassword123\n")
            until(b"LOGIN OK")
            self.assertEqual(players[1].call(0x106, struct.pack("!Q", target.user_id))[0], 0)
            until(b"INVITE_RECEIVED")
            send(f"accept {code}\n")
            until(b"JOIN_ROOM code=")
            send("quit\n")
            rest, error = process.communicate(timeout=5)
            output.extend(rest)
            self.assertEqual(process.returncode, 0, error.decode())
        finally:
            if process.poll() is None:
                process.terminate(); process.communicate(timeout=5)

    def test_z_history_survives_server_and_postgres_restart(self):
        players, _ = self.lobby()
        match_id = self.play(players)
        username = players[0].username
        for p in self.peers:
            p.close()
        self.stop_server()
        subprocess.run([pg_binary("pg_ctl"), "-D", str(self.data), "-l", str(self.temp / "db.log"),
                        "-m", "fast", "-w", "restart"], check=True, capture_output=True, text=True)
        self.start_server()
        p = RoomClient("127.0.0.1", self.port); self.peers.append(p)
        self.assertEqual(p.call(2, credentials(username))[0], 0)
        self.assertEqual(self.history(p)["rows"][0]["match_id"], match_id)
        self.assertEqual(self.stats(p)["finished"], 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    test_auth.BUILD = (test_auth.ROOT / args.build_dir).resolve()
    unittest.main(argv=[os.path.basename(__file__)], verbosity=2)
