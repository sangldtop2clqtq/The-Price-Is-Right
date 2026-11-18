"""Create/reuse dedicated demo accounts and verify a live room without manual terminals."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import re
import secrets
import select
import time
import fcntl
import math

from room_client import RoomClient, string

ROOT = Path(__file__).resolve().parents[1]


def expect(status, expected, action):
    if status != expected:
        raise RuntimeError(f"{action}: status={status}, expected={expected}")


def account_password(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    with os.fdopen(fd, "r+") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        os.fchmod(stream.fileno(), 0o600)
        content = stream.read()
        if content:
            data = json.loads(content)
            password = data.get("password") if isinstance(data, dict) else None
            if not isinstance(password, str) or not 8 <= len(password.encode("utf-8")) <= 128:
                raise ValueError("saved demo credential file is invalid; keep the original file to reuse accounts")
            return password
        password = secrets.token_urlsafe(24)
        json.dump({"password": password}, stream)
        stream.flush()
        os.fsync(stream.fileno())
        return password


def run(args):
    if args.players < 2 or args.players > 4 or args.spectators < 0 or args.players + args.spectators > 64:
        raise ValueError("need 2..4 players and at most 64 total connections")
    if args.spectators and args.players != 4:
        raise ValueError("automatic spectator assignment requires all four PLAYER slots filled")
    if args.exercise_fifo and not args.spectators:
        raise ValueError("--exercise-fifo requires at least one spectator")
    if not 1 <= args.port <= 65535:
        raise ValueError("port must be in 1..65535")
    if not re.fullmatch(r"[A-Za-z0-9_]{3,16}", args.account_prefix) or not math.isfinite(args.hold) or args.hold < 0:
        raise ValueError("prefix must contain 3..16 ASCII letters/digits/underscores; hold must be nonnegative")
    password = account_password(args.accounts_file)
    peers = []
    with contextlib.ExitStack() as stack:
        for index in range(args.players + args.spectators):
            peer = RoomClient(args.host, args.port)
            stack.callback(peer.close)
            peers.append(peer)
            username = f"{args.account_prefix}_{index + 1}"
            payload = string(username) + string(password)
            deadline = time.monotonic() + 10
            while True:
                status, _ = peer.call(2, payload)
                if status != 13 or time.monotonic() >= deadline:
                    break
                time.sleep(0.05)  # retry read-only LOGIN while DB initializes
            if status == 3:
                expect(peer.call(1, payload)[0], 0, "register demo account")
                status, _ = peer.call(2, payload)
            if status == 5:
                raise RuntimeError("demo account already online; use another --account-prefix or close the previous demo")
            expect(status, 0, "login demo account (check saved credential file if an existing account has another password)")
        host = peers[0]
        status, body = host.call(0x101)
        expect(status, 0, "create room")
        code = host.view["code"]
        for peer in peers[1:args.players]:
            expect(peer.call(0x102, string(code))[0], 0, "join PLAYER")
            if peer.view["role"] != 0:
                raise RuntimeError("expected PLAYER role")
        for peer in peers[:args.players]:
            expect(peer.call(0x104, b"\1")[0], 0, "ready PLAYER")
        for peer in peers[args.players:]:
            expect(peer.call(0x102, string(code))[0], 0, "join SPECTATOR")
            if peer.view["role"] != 1:
                raise RuntimeError("expected SPECTATOR role")
            expect(peer.call(0x104, b"\1")[0], 10, "reject spectator ready")
        for peer in peers:
            view = peer.update(lambda v: len(v["players"]) == args.players and
                               v["spectators"] == args.spectators and all(p["ready"] for p in v["players"]))
            expected_role = 0 if peer in peers[:args.players] else 1
            if view["role"] != expected_role or view["host"] != host.user_id:
                raise RuntimeError("room snapshot/host/role mismatch")
        expect(peers[1].call(0x201)[0], 8, "non-host start guard")
        print(f"PASS room={code}: {args.players} PLAYER ready, {args.spectators} SPECTATOR; snapshots consistent", flush=True)
        print("Lobby verified; use make demo-game to play five rounds", flush=True)
        if args.exercise_fifo:
            expect(peers[1].call(0x103)[0], 0, "leave PLAYER")
            promoted = peers[args.players].update(lambda v: v["role"] == 0)
            if any(p["ready"] for p in promoted["players"]) or promoted["spectators"] != args.spectators - 1:
                raise RuntimeError("FIFO promotion must reset PLAYER readiness")
            print("PASS FIFO: earliest spectator promoted to PLAYER; readiness reset", flush=True)
        if args.hold:
            print(f"Keeping connections for {args.hold:g}s; Ctrl+C closes only these demo clients", flush=True)
            deadline = time.monotonic() + args.hold
            while time.monotonic() < deadline:
                ready, _, _ = select.select([p.sock for p in peers], [], [], max(0.0, min(0.5, deadline - time.monotonic())))
                for peer in peers:
                    if peer.sock in ready:
                        kind, event, _, payload = peer.receive(time.monotonic() + 5)
                        if kind != 2:
                            raise RuntimeError("unexpected response during hold")
                        view = peer.event(event, payload)
                        print(f"UPDATE user={peer.user_id} role={view['role']} revision={view['revision']}", flush=True)
    print("Demo finished; connections closed, accounts retained for reruns", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6666)
    parser.add_argument("--players", type=int, default=4)
    parser.add_argument("--spectators", type=int, default=0)
    parser.add_argument("--account-prefix", default="demo_auto")
    parser.add_argument("--accounts-file", type=Path, default=ROOT / ".local/demo_accounts.json")
    parser.add_argument("--exercise-fifo", action="store_true")
    parser.add_argument("--hold", type=float, default=0)
    args = parser.parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print("Demo stopped; connections closed")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Demo failed: {error}\n")


if __name__ == "__main__":
    main()
