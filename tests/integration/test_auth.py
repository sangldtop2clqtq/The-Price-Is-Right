"""Auth integration with an isolated, disposable real PostgreSQL cluster."""
import argparse
import concurrent.futures
import contextlib
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import json

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from dev_db import apply_migrations, apply_seeds, pg_binary, sql  # noqa: E402
from test_transport import frame, receive  # noqa: E402

BUILD = ROOT / "build"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def credentials(username, password="password123"):
    def string(value):
        data = value.encode("utf-8") if isinstance(value, str) else value
        return struct.pack("!H", len(data)) + data
    return string(username) + string(password)


def request(sock, code, payload=b"", request_id=1):
    sock.sendall(frame(code=code, request_id=request_id, payload=payload))
    response_code, response_id, body = receive(sock)
    if (response_code, response_id) != (code, request_id) or len(body) < 2:
        raise AssertionError("response correlation/status missing")
    status, = struct.unpack("!H", body[:2])
    if status and len(body) != 2:
        raise AssertionError("error response has unexpected body")
    return status, body[2:]


class DatabaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stack = contextlib.ExitStack()
        cls.addClassCleanup(cls.stack.close)
        cls.temp = Path(cls.stack.enter_context(tempfile.TemporaryDirectory(prefix="pr-auth-", dir="/tmp")))
        cls.data = cls.temp / "data"
        sockets = cls.temp / "socket"
        sockets.mkdir()
        db_port = free_port()
        subprocess.run([pg_binary("initdb"), "-D", str(cls.data), "-U", "price_game",
                        "--auth=trust", "--encoding=UTF8", "--locale=C"],
                       check=True, capture_output=True, text=True)
        subprocess.run([pg_binary("pg_ctl"), "-D", str(cls.data), "-l", str(cls.temp / "db.log"),
                        "-o", f"-h 127.0.0.1 -p {db_port} -k {sockets}", "-w", "start"],
                       check=True, capture_output=True, text=True)
        cls.stack.callback(lambda: subprocess.run(
            [pg_binary("pg_ctl"), "-D", str(cls.data), "-m", "fast", "-w", "stop"],
            check=True, capture_output=True, text=True))
        cls.url = f"hostaddr=127.0.0.1 port={db_port} dbname=postgres user=price_game connect_timeout=5"
        apply_migrations(cls.url)
        apply_seeds(cls.url)
        cls.process = None
        cls.log_path = cls.temp / "server.jsonl"
        cls.start_server()
        cls.stack.callback(cls.stop_server)

    @classmethod
    def connect(cls):
        sock = socket.create_connection(("127.0.0.1", cls.port), timeout=10)
        sock.settimeout(15)
        return sock

    @classmethod
    def start_server(cls):
        cls.port = free_port()
        server_env = dict(os.environ, PR_DATABASE_URL=cls.url + " application_name=pr_game_tests",
                          PR_LOG_FILE=str(cls.log_path), PR_LOG_LEVEL="info")
        server_env.update(getattr(cls, "server_env", {}))
        cls.process = subprocess.Popen(
            [str(BUILD / "pr_server"), "127.0.0.1", str(cls.port)],
            env=server_env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                break
            try:
                with cls.connect() as sock:
                    status, _ = request(sock, 2, credentials("readiness_probe"))
                    if status == 3:
                        return
            except (OSError, EOFError):
                pass
            time.sleep(0.03)
        cls.stop_server()
        raise RuntimeError("auth/database did not become ready")

    @classmethod
    def stop_server(cls):
        if cls.process is None:
            return
        process, cls.process = cls.process, None
        process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=10)
        if process.returncode != 0 or "Sanitizer" in stderr or "runtime error:" in stderr:
            raise AssertionError(f"server failed: {process.returncode}\n{stdout}\n{stderr}")

    def register(self, sock, username, password="password123"):
        status, body = request(sock, 1, credentials(username, password))
        self.assertEqual(status, 0)
        self.assertEqual(len(body), 8)
        user_id, = struct.unpack("!Q", body)
        self.assertGreater(user_id, 0)
        return user_id


class AuthTests(DatabaseTests):
    def test_db_backend_loss_recovers_and_ping_continues(self):
        sql(self.url, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='pr_game_tests'")
        with self.connect() as active:
            self.assertEqual(request(active, 0xf0, struct.pack("!I", 7)), (0, struct.pack("!I", 7)))
            deadline = time.monotonic() + 8
            while True:
                status, _ = request(active, 2, credentials("readiness_probe"))
                if status == 3 or time.monotonic() > deadline:
                    break
                time.sleep(0.05)
            self.assertEqual(status, 3)

    def test_half_close_waits_for_async_auth(self):
        with self.connect() as sock:
            sock.sendall(frame(1, 1, credentials("half_closed_auth")) +
                         frame(2, 2, credentials("half_closed_auth")) + frame(3, 3, b""))
            sock.shutdown(socket.SHUT_WR)
            for code in (1, 2, 3):
                received_code, request_id, body = receive(sock)
                self.assertEqual((received_code, request_id, body[:2]), (code, code, b"\0\0"))
            self.assertEqual(sock.recv(1), b"")

    def test_structured_logs_contain_context_without_credentials(self):
        with self.connect() as sock:
            self.register(sock, "log_context", "never_log_this_password")
            self.assertEqual(request(sock, 2, credentials("log_context", "never_log_this_password"))[0], 0)
        text = self.log_path.read_text()
        self.assertNotIn("never_log_this_password", text)
        self.assertNotIn("$argon2", text)
        self.assertNotIn("log_context", text)
        records = [json.loads(line) for line in text.splitlines()]
        responses = [row for row in records if row["event"] == "response"]
        self.assertTrue(any(row["code"] == 2 and row["user_id"] > 0 and row["status"] == 0 for row in responses))
        for row in responses:
            self.assertGreater(row["connection_id"], 0)
            self.assertGreater(row["request_id"], 0)
            self.assertGreaterEqual(row["elapsed_us"], 0)
            self.assertIn("room_code", row)

    def test_account_lifecycle_and_guards(self):
        with self.connect() as sock:
            self.assertEqual(request(sock, 3)[0], 2)
            self.assertEqual(request(sock, 0x302)[0], 2)
            user_id = self.register(sock, "lifecycle")
            self.assertEqual(request(sock, 1, credentials("lifecycle"))[0], 4)
            self.assertEqual(request(sock, 2, credentials("lifecycle", "wrongpass"))[0], 3)
            self.assertEqual(request(sock, 2, credentials("not_registered"))[0], 3)
            status, body = request(sock, 2, credentials("lifecycle"))
            self.assertEqual(status, 0)
            self.assertEqual(body, struct.pack("!QH", user_id, 9) + b"lifecycle")
            self.assertEqual(request(sock, 2, credentials("lifecycle"))[0], 10)
            self.assertEqual(request(sock, 1, credentials("another_user"))[0], 10)
            self.assertEqual(request(sock, 0x302)[0], 0)
            self.assertEqual(request(sock, 3, b"extra")[0], 1)
            self.assertEqual(request(sock, 3), (0, b""))
            self.assertEqual(request(sock, 0x302)[0], 2)
            self.assertEqual(request(sock, 2, credentials("lifecycle"))[0], 0)

    def test_atomic_duplicate_registration(self):
        def attempt(_):
            with self.connect() as sock:
                return request(sock, 1, credentials("race_register"))[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            statuses = list(pool.map(attempt, range(4)))
        self.assertEqual(sorted(statuses), [0, 4, 4, 4])
        self.assertEqual(sql(self.url, "SELECT count(*) FROM users WHERE username='race_register'").strip(), "1")

    def test_concurrent_login_one_session_and_logout_release(self):
        with self.connect() as seed:
            self.register(seed, "race_login")
        with contextlib.ExitStack() as stack:
            sockets = [stack.enter_context(self.connect()) for _ in range(3)]
            with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
                statuses = list(pool.map(lambda s: request(s, 2, credentials("race_login"))[0], sockets))
            self.assertEqual(sorted(statuses), [0, 5, 5])
            winner = sockets[statuses.index(0)]
            loser = sockets[statuses.index(5)]
            self.assertEqual(request(winner, 3)[0], 0)
            self.assertEqual(request(loser, 2, credentials("race_login"))[0], 0)

    def test_disconnect_cleans_session(self):
        with self.connect() as sock:
            self.register(sock, "disconnect_login")
            self.assertEqual(request(sock, 2, credentials("disconnect_login"))[0], 0)
        deadline = time.monotonic() + 3
        with self.connect() as sock:
            while True:
                status, _ = request(sock, 2, credentials("disconnect_login"))
                if status != 5 or time.monotonic() >= deadline:
                    break
                time.sleep(0.02)
            self.assertEqual(status, 0)

    def test_invalid_payloads_and_utf8(self):
        payloads = [b"", credentials("aa"), credentials("bad'user"), credentials("valid", "short"),
                    credentials("valid", b"abcdefgh\x00"), credentials("valid", b"abcdefgh\xc0\x80"),
                    credentials("valid") + b"extra", credentials("valid")[:-1],
                    credentials("a" * 33), credentials("valid", "x" * 129)]
        with self.connect() as sock:
            for code in (1, 2):
                for payload in payloads:
                    with self.subTest(code=code, payload_length=len(payload)):
                        self.assertEqual(request(sock, code, payload)[0], 1)
            user_id = self.register(sock, "utf8_password", "mật khẩu tốt 123")
            self.assertEqual(request(sock, 2, credentials("utf8_password", "mật khẩu tốt 123"))[0], 0)
            self.assertGreater(user_id, 0)

    def test_pipeline_register_login_logout_preserves_order(self):
        with self.connect() as sock:
            sock.sendall(frame(1, 11, credentials("pipeline")) + frame(2, 12, credentials("pipeline")) +
                         frame(3, 13, b"") + frame(0x101, 14, b""))
            results = [receive(sock) for _ in range(4)]
            self.assertEqual([(c, rid, struct.unpack("!H", b[:2])[0]) for c, rid, b in results],
                             [(1, 11, 0), (2, 12, 0), (3, 13, 0), (0x101, 14, 2)])

    def test_password_hashes_salted_and_not_plaintext(self):
        with self.connect() as sock:
            self.register(sock, "salt_one")
            self.register(sock, "salt_two")
        rows = sql(self.url, "SELECT password_hash FROM users WHERE username IN ('salt_one','salt_two') ORDER BY username").splitlines()
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row.startswith("$argon2id$v=19$") and "password123" not in row for row in rows))
        self.assertNotEqual(rows[0], rows[1])

    def test_disconnect_pending_does_not_authenticate_reused_slot(self):
        with self.connect() as seed:
            self.register(seed, "stale_callback")
        for _ in range(8):
            old = self.connect()
            old.sendall(frame(2, 1, credentials("stale_callback")))
            time.sleep(0.02)
            old.close()
            with self.connect() as replacement:
                self.assertEqual(request(replacement, 0x101)[0], 2)
                time.sleep(0.08)
                self.assertEqual(request(replacement, 0x101)[0], 2)

    def test_disconnect_during_confirmed_db_job(self):
        sql(self.url, "CREATE FUNCTION test_delay_registration() RETURNS trigger LANGUAGE plpgsql AS $$ "
                      "BEGIN IF NEW.username='delayed_disconnect' THEN PERFORM pg_sleep(0.8); END IF; RETURN NEW; END $$; "
                      "CREATE TRIGGER test_delay_registration BEFORE INSERT ON users "
                      "FOR EACH ROW EXECUTE FUNCTION test_delay_registration();")
        old = self.connect()
        try:
            old.sendall(frame(1, 1, credentials("delayed_disconnect")))
            deadline = time.monotonic() + 5
            active = "0"
            while time.monotonic() < deadline:
                active = sql(self.url, "SELECT count(*) FROM pg_stat_activity WHERE wait_event='PgSleep' "
                             "AND query LIKE 'INSERT INTO users%'").strip()
                if active != "0":
                    break
                time.sleep(0.01)
            self.assertNotEqual(active, "0", "registration must be in flight before disconnect")
            old.close()
            with self.connect() as replacement:
                self.assertEqual(request(replacement, 0x101)[0], 2)
                time.sleep(1)
                # Any response from the stale callback would break request correlation here.
                self.assertEqual(request(replacement, 0x101, request_id=88)[0], 2)
        finally:
            old.close()
            sql(self.url, "DROP TRIGGER test_delay_registration ON users; DROP FUNCTION test_delay_registration();")

    def test_slow_database_and_hashing_do_not_block_ping(self):
        # Hold INSERTs behind a DB lock, then prove another client can still PING.
        blocker = subprocess.Popen([pg_binary("psql"), "-X", "-d", self.url, "-v", "ON_ERROR_STOP=1", "-c",
            "BEGIN; SET application_name='pr_auth_lock'; LOCK TABLE users IN ACCESS EXCLUSIVE MODE; SELECT pg_sleep(30); COMMIT;"],
            env=dict(os.environ, PGDATABASE=self.url), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                held = sql(self.url, "SELECT count(*) FROM pg_locks l JOIN pg_stat_activity a ON l.pid=a.pid "
                           "WHERE a.application_name='pr_auth_lock' AND l.mode='AccessExclusiveLock' AND l.granted").strip()
                if held != "0":
                    break
                time.sleep(0.02)
            self.assertNotEqual(held, "0")
            with self.connect() as busy, self.connect() as active:
                busy.sendall(frame(1, 1, credentials("slow_db")))
                for _ in range(10):
                    start = time.monotonic()
                    self.assertEqual(request(active, 0xF0, struct.pack("!I", 123)), (0, struct.pack("!I", 123)))
                    self.assertLess(time.monotonic() - start, 1.0)
                # Force the adapter's 5s timeout rather than releasing the lock early.
                code, request_id, body = receive(busy)
                self.assertEqual((code, request_id, body), (1, 1, struct.pack("!H", 13)))
        finally:
            sql(self.url, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='pr_auth_lock'")
            blocker.communicate(timeout=5)
        deadline = time.monotonic() + 6
        with self.connect() as sock:
            while True:
                status, _ = request(sock, 2, credentials("readiness_probe"))
                if status == 3 or time.monotonic() > deadline:
                    break
                time.sleep(0.05)
            self.assertEqual(status, 3)

    def test_schema_seed_reentrant(self):
        apply_migrations(self.url)
        apply_seeds(self.url)
        self.assertEqual(sql(self.url, "SELECT count(*) FROM products").strip(), "30")
        self.assertEqual(sql(self.url, "SELECT count(*) FROM schema_migrations WHERE version=1").strip(), "1")

    def test_z_cli_and_persistence_after_server_restart(self):
        commands = "register cli_account\npassword123\nlogin cli_account\npassword123\nlogout\nquit\n"
        result = subprocess.run([str(BUILD / "pr_cli"), "127.0.0.1", str(self.port), "--interactive"],
                                input=commands, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        for expected in ("REGISTER OK", "LOGIN OK", "LOGOUT OK"):
            self.assertIn(expected, result.stdout)
        self.assertNotIn("password123", result.stdout + result.stderr)
        self.stop_server()
        subprocess.run([pg_binary("pg_ctl"), "-D", str(self.data), "-l", str(self.temp / "db.log"),
                        "-m", "fast", "-w", "restart"], check=True, capture_output=True, text=True)
        self.start_server()
        with self.connect() as sock:
            self.assertEqual(request(sock, 2, credentials("cli_account"))[0], 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    BUILD = (ROOT / args.build_dir).resolve()
    unittest.main(argv=[os.path.basename(__file__)], verbosity=2)
