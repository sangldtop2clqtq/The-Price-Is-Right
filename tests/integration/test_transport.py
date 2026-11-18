"""Black-box TCP tests against the real server; standard library only."""
import argparse
import concurrent.futures
import contextlib
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time
import unittest
import tempfile
import json

HEADER = struct.Struct("!HBBHHI")
BUILD = Path("build").resolve()


def frame(code=0x00F0, request_id=1, payload=struct.pack("!I", 42), kind=0):
    return HEADER.pack(0x5052, 1, kind, code, len(payload), request_id) + payload


def exact(sock, n):
    result = bytearray()
    while len(result) < n:
        chunk = sock.recv(n - len(result))
        if not chunk:
            raise EOFError("connection closed within frame")
        result.extend(chunk)
    return bytes(result)


def receive(sock):
    magic, version, kind, code, length, request_id = HEADER.unpack(exact(sock, 12))
    if (magic, version, kind) != (0x5052, 1, 1) or length > 4096:
        raise AssertionError("invalid response header")
    return code, request_id, exact(sock, length)


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="pr-transport-", dir="/tmp")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.log_path = Path(cls.temp.name) / "server.jsonl"
        # Choose an ephemeral port. Startup retries handle the small release/bind race.
        cls.process = None
        for _ in range(5):
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                cls.port = probe.getsockname()[1]
            cls.process = subprocess.Popen(
                [str(BUILD / "pr_server"), "127.0.0.1", str(cls.port)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                env=dict({key: value for key, value in os.environ.items() if key != "PR_DATABASE_URL"},
                         PR_LOG_FILE=str(cls.log_path), PR_LOG_LEVEL="debug", PR_FRAME_TIMEOUT_MS="500"),
            )
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if cls.process.poll() is not None:
                    break
                try:
                    with cls.connect():
                        return
                except OSError:
                    time.sleep(0.02)
            cls.process.terminate()
            output = cls.process.communicate(timeout=5)
            if "Address already in use" not in output[1]:
                raise RuntimeError(f"server startup failed: {output}")
        raise RuntimeError("could not reserve test port")

    @classmethod
    def tearDownClass(cls):
        cls.process.send_signal(signal.SIGTERM)
        stdout, stderr = cls.process.communicate(timeout=5)
        if cls.process.returncode != 0 or "Sanitizer" in stderr or "runtime error:" in stderr:
            raise AssertionError(f"server exit={cls.process.returncode}\n{stdout}\n{stderr}")

    @classmethod
    def connect(cls):
        sock = socket.create_connection(("127.0.0.1", cls.port), timeout=3)
        sock.settimeout(3)
        return sock

    def assert_pong(self, sock, request_id, nonce):
        self.assertEqual(receive(sock), (0xF0, request_id, struct.pack("!HI", 0, nonce)))

    def test_four_persistent_clients(self):
        with contextlib.ExitStack() as stack:
            clients = [stack.enter_context(self.connect()) for _ in range(4)]
            for iteration in range(4):
                for index, sock in enumerate(clients):
                    sock.sendall(frame(request_id=iteration + 1, payload=struct.pack("!I", index)))
                for index, sock in enumerate(clients):
                    self.assert_pong(sock, iteration + 1, index)

    def test_incomplete_client_does_not_block_others(self):
        with self.connect() as partial, self.connect() as active:
            data = frame()
            partial.sendall(data[:5])
            active.sendall(frame(request_id=77))
            self.assert_pong(active, 77, 42)
            partial.sendall(data[5:])
            self.assert_pong(partial, 1, 42)

    def test_fragmentation_coalescing_and_max_payload(self):
        with self.connect() as sock:
            for byte in frame():
                sock.sendall(bytes([byte]))
            self.assert_pong(sock, 1, 42)
            sock.sendall(b"".join(frame(request_id=i) for i in range(2, 102)))
            for i in range(2, 102):
                self.assert_pong(sock, i, 42)
            sock.sendall(frame(code=0xFFFF, request_id=102, payload=b"x" * 4096))
            self.assertEqual(receive(sock), (0xFFFF, 102, struct.pack("!H", 1)))
            sock.sendall(frame(request_id=103))
            self.assert_pong(sock, 103, 42)

    def test_errors_keep_connection_and_echo_ids(self):
        with self.connect() as sock:
            sock.sendall(frame(request_id=31, payload=b"short"))
            self.assertEqual(receive(sock), (0xF0, 31, struct.pack("!H", 1)))
            sock.sendall(frame(code=2, request_id=32, payload=b""))
            self.assertEqual(receive(sock), (2, 32, struct.pack("!H", 1)))
            sock.sendall(frame(request_id=33))
            self.assert_pong(sock, 33, 42)

    def test_invalid_headers_close_only_offending_client(self):
        bad_headers = [
            HEADER.pack(0, 1, 0, 0xF0, 0, 1),
            HEADER.pack(0x5052, 2, 0, 0xF0, 0, 1),
            HEADER.pack(0x5052, 1, 3, 0xF0, 0, 1),
            HEADER.pack(0x5052, 1, 0, 0xF0, 4097, 1),
            HEADER.pack(0x5052, 1, 0, 0xF0, 0, 0),
            HEADER.pack(0x5052, 1, 1, 0xF0, 0, 1),
            HEADER.pack(0x5052, 1, 2, 0xF0, 0, 0),
        ]
        for header in bad_headers:
            with self.subTest(header=header.hex()), self.connect() as bad:
                bad.sendall(header)
                try:
                    self.assertEqual(bad.recv(1), b"")
                except ConnectionResetError:
                    pass
            with self.connect() as good:
                good.sendall(frame())
                self.assert_pong(good, 1, 42)

    def test_disconnect_mid_frame_and_repeated_connections(self):
        for _ in range(50):
            with self.connect() as sock:
                sock.sendall(frame()[:13])
        with self.connect() as sock:
            sock.sendall(frame())
            self.assert_pong(sock, 1, 42)

    def test_forty_concurrent_clients(self):
        # Keep all connections open together, then issue requests concurrently.
        with contextlib.ExitStack() as stack:
            clients = [stack.enter_context(self.connect()) for _ in range(40)]
            def exchange(pair):
                index, sock = pair
                for turn in range(10):
                    sock.sendall(frame(request_id=turn + 1, payload=struct.pack("!I", index)))
                    self.assert_pong(sock, turn + 1, index)
            with concurrent.futures.ThreadPoolExecutor(max_workers=40) as pool:
                list(pool.map(exchange, enumerate(clients)))

    def test_real_cli_batch_and_interactive(self):
        for args, stdin, count in [(["64"], None, 64), (["--interactive"], "ping 7\nping 8\nquit\n", 2)]:
            result = subprocess.run(
                [str(BUILD / "pr_cli"), "127.0.0.1", str(self.port), *args],
                input=stdin, capture_output=True, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.count("PONG request_id="), count)

    def test_half_close_drains_accepted_requests(self):
        with self.connect() as sock:
            sock.sendall(frame(request_id=1) + frame(request_id=2))
            sock.shutdown(socket.SHUT_WR)
            self.assert_pong(sock, 1, 42)
            self.assert_pong(sock, 2, 42)
            self.assertEqual(sock.recv(1), b"")

    def test_incomplete_frame_timeout_frees_connection(self):
        with self.connect() as slow, self.connect() as active:
            slow.sendall(frame()[:3])
            start = time.monotonic()
            active.sendall(frame())
            self.assert_pong(active, 1, 42)
            self.assertLess(time.monotonic() - start, 1)
            self.assertEqual(slow.recv(1), b"")
        logs = [json.loads(line) for line in self.log_path.read_text().splitlines()]
        self.assertTrue(any(row["reason"] == "frame_timeout" for row in logs))

    def test_request_queue_overflow_does_not_stop_other_clients(self):
        with self.connect() as flood:
            packet = b"".join(frame(code=0xffff, request_id=i, payload=b"") for i in range(1, 6001))
            try:
                flood.sendall(packet)
                while flood.recv(8192):
                    pass
            except (BrokenPipeError, ConnectionResetError):
                pass
        with self.connect() as active:
            active.sendall(frame())
            self.assert_pong(active, 1, 42)
        logs = [json.loads(line) for line in self.log_path.read_text().splitlines()]
        self.assertTrue(any(row["reason"] == "request_queue_limit" for row in logs))

    def test_full_log_pipe_does_not_stall_network(self):
        port = 0
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        env = {key: value for key, value in os.environ.items() if key not in ("PR_DATABASE_URL", "PR_LOG_FILE")}
        env["PR_LOG_LEVEL"] = "debug"
        process = subprocess.Popen([str(BUILD / "pr_server"), "127.0.0.1", str(port)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise
                    time.sleep(0.02)
            with sock:
                for _ in range(100):
                    sock.sendall(b"".join(frame(request_id=i) for i in range(1, 65)))
                    for i in range(1, 65):
                        self.assert_pong(sock, i, 42)
        finally:
            process.terminate()
            _, error = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 0, error.decode(errors="replace"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    BUILD = Path(args.build_dir).resolve()
    unittest.main(argv=[os.path.basename(__file__)], verbosity=2)
