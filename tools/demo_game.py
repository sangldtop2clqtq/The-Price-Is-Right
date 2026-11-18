"""Play all five rounds with dedicated demo accounts, over the public TCP protocol."""
import argparse
import contextlib
from pathlib import Path
import re
import struct

from demo_room import account_password, expect, ROOT
from room_client import RoomClient, string


def run(args):
    if args.players < 2 or args.players > 4 or args.spectators < 0 or args.players + args.spectators > 64:
        raise ValueError("need 2..4 players, at most 64 connections")
    if not re.fullmatch(r"[A-Za-z0-9_]{3,16}", args.account_prefix) or not 0 <= args.guess <= 10**12:
        raise ValueError("invalid account prefix or guess")
    password = account_password(args.accounts_file)
    with contextlib.ExitStack() as stack:
        peers = []
        for i in range(args.players + args.spectators):
            peer = RoomClient(args.host, args.port)
            stack.callback(peer.close); peers.append(peer)
            payload = string(f"{args.account_prefix}_{i+1}") + string(password)
            status, _ = peer.call(2, payload)
            if status == 3:
                expect(peer.call(1, payload)[0], 0, "register game demo")
                status, _ = peer.call(2, payload)
            expect(status, 0, "login game demo; wait for DB ready and use a distinct prefix for simultaneous demos")
        host = peers[0]
        expect(host.call(0x101)[0], 0, "create")
        code = host.view["code"]
        for peer in peers[1:args.players]:
            expect(peer.call(0x102, string(code))[0], 0, "join")
        for peer in peers[:args.players]:
            expect(peer.call(0x104, b"\1")[0], 0, "ready")
        status, body = host.call(0x201)
        expect(status, 0, "start")
        match_id, = struct.unpack("!Q", body)
        print(f"GAME room={code} match_id={match_id}; playing five rounds using server timers", flush=True)
        game_room_revision = 0
        for round_no in range(1, 6):
            for index, peer in enumerate(peers[:args.players]):
                view = peer.wait_game(0x8202, lambda v: v["round_no"] == round_no)
                if index == 0:
                    print(f"ROUND {round_no}: {'SINGLE' if view['type'] == 0 else 'BASKET'}, duration_ms={view['duration_ms']}", flush=True)
                    if round_no == 1:
                        game_room_revision = host.view["revision"]
                        for spectator in peers[args.players:]:
                            expect(spectator.call(0x102, string(code))[0], 0, "join spectator during game")
                            snapshot = spectator.wait_game(0x8205)
                            if snapshot["match_id"] != match_id or snapshot["has_result"]:
                                raise RuntimeError("spectator must receive current public state without secret prices")
                if not args.skip_last or index != args.players - 1:
                    expect(peer.call(0x202, struct.pack("!QBQ", match_id, round_no, args.guess))[0], 0, "submit guess")
            for peer in peers:
                result = peer.wait_game(0x8203, lambda v: v["round_no"] == round_no)
                if peer is host:
                    print(f"RESULT {round_no}: actual_total={result['actual_total']} "
                          f"scores={[p['score'] for p in result['scores']]}", flush=True)
        final = None
        for peer in peers:
            over = peer.wait_game(0x8204)
            if over["state"] != 2 or over["abort_reason"]:
                raise RuntimeError("expected FINISHED match")
            if final is not None and over != final:
                raise RuntimeError("final rankings differ between clients")
            final = over
            waiting = peer.update(lambda v: v["state"] == 0 and v["revision"] > game_room_revision)
            if any(p["ready"] for p in waiting["players"]):
                raise RuntimeError("ready must reset after match")
        print(f"PASS GAME_OVER: rankings={final['rankings']}; room WAITING, results saved in PostgreSQL", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6666)
    parser.add_argument("--players", type=int, default=4)
    parser.add_argument("--spectators", type=int, default=1)
    parser.add_argument("--account-prefix", default="game_auto")
    parser.add_argument("--accounts-file", type=Path, default=ROOT / ".local/demo_accounts.json")
    parser.add_argument("--guess", type=int, default=100000)
    parser.add_argument("--skip-last", action="store_true")
    args = parser.parse_args()
    try:
        run(args)
    except KeyboardInterrupt:
        print("Game demo stopped; connections closed")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Game demo failed: {error}\n")


if __name__ == "__main__":
    main()
