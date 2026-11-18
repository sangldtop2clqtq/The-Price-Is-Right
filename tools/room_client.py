"""Small synchronous diagnostic client for scripts; no test or database dependency."""
import collections
import socket
import struct
import time
from game_protocol import game_view
from account_protocol import invite_view

HEADER = struct.Struct("!HBBHHI")


def string(text):
    data = text.encode("utf-8")
    if len(data) > 65535:
        raise ValueError("string too long")
    return struct.pack("!H", len(data)) + data


def room_view(payload):
    offset = 0
    def number(fmt):
        nonlocal offset
        value, = struct.unpack_from("!" + fmt, payload, offset)
        offset += struct.calcsize("!" + fmt)
        return value
    def text():
        nonlocal offset
        length = number("H")
        if offset + length > len(payload):
            raise ValueError("truncated string")
        result = payload[offset:offset + length].decode("utf-8")
        offset += length
        return result
    view = dict(code=text(), state=number("B"), host=number("Q"), revision=number("I"),
                role=number("B"), spectators=number("I"))
    count = number("H")
    if count > 4:
        raise ValueError("too many players")
    view["players"] = [dict(id=number("Q"), name=text(), ready=number("B"), connected=number("B")) for _ in range(count)]
    if offset != len(payload):
        raise ValueError("unexpected room bytes")
    return view


class RoomClient:
    def __init__(self, host, port, timeout=15):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.timeout = timeout
        self.next_id = 1
        self.events = collections.deque(maxlen=128)
        self.user_id = None
        self.view = None
        self.game_events = collections.deque(maxlen=128)
        self.invitations = collections.deque(maxlen=128)

    def close(self):
        self.sock.close()

    def exact(self, n, deadline):
        data = bytearray()
        while len(data) < n:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("server response timeout")
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("server disconnected")
            data.extend(chunk)
        return bytes(data)

    def receive(self, deadline):
        magic, version, kind, code, length, request_id = HEADER.unpack(self.exact(12, deadline))
        if magic != 0x5052 or version != 1 or length > 4096 or kind not in (1, 2):
            raise ValueError("invalid server frame")
        if (kind == 2) != (request_id == 0):
            raise ValueError("invalid response/event id")
        return kind, code, request_id, self.exact(length, deadline)

    def event(self, code, payload):
        if code == 0x8102:
            view = invite_view(payload)
            self.invitations.append(view)
            return view
        if code == 0x0202:
            return None
        if code != 0x8101:
            view = game_view(code, payload)
            self.game_events.append((code, view))
            return view
        view = room_view(payload)
        self.view = view
        self.events.append(view)
        return view

    def call(self, code, payload=b""):
        if len(payload) > 4096:
            raise ValueError("payload too large")
        request_id = self.next_id
        self.next_id = (request_id + 1) & 0xffffffff or 1
        self.sock.settimeout(self.timeout)
        self.sock.sendall(HEADER.pack(0x5052, 1, 0, code, len(payload), request_id) + payload)
        deadline = time.monotonic() + self.timeout
        while True:
            kind, reply_code, reply_id, body = self.receive(deadline)
            if kind == 2:
                self.event(reply_code, body)
                continue
            if (reply_code, reply_id) != (code, request_id) or len(body) < 2:
                raise ValueError("response correlation failed")
            status, = struct.unpack("!H", body[:2])
            if status and len(body) != 2:
                raise ValueError("invalid error response")
            if not status and code in (0x101, 0x102):
                self.view = room_view(body[2:])
            if not status and code == 2:
                if len(body) < 12:
                    raise ValueError("invalid login result")
                self.user_id, = struct.unpack("!Q", body[2:10])
            return status, body[2:]

    def update(self, predicate):
        deadline = time.monotonic() + self.timeout
        while True:
            if self.events:
                view = self.events.popleft()
            else:
                kind, code, request_id, body = self.receive(deadline)
                if kind != 2:
                    raise ValueError("unexpected response while watching room")
                view = self.event(code, body)
                if code != 0x8101:
                    continue
                self.events.pop()  # consume the view just handled
            if predicate(view):
                return view

    def wait_game(self, code, predicate=lambda view: True, timeout=45):
        deadline = time.monotonic() + timeout
        while True:
            if self.game_events:
                event_code, view = self.game_events.popleft()
            else:
                kind, event_code, _, body = self.receive(deadline)
                if kind != 2:
                    raise ValueError("unexpected response while watching game")
                view = self.event(event_code, body)
                if event_code in (0x8101, 0x8102, 0x0202):
                    continue
                self.game_events.pop()
            if event_code == 0x8206 and code != 0x8206:
                raise RuntimeError("match results could not be saved")
            if event_code == code and predicate(view):
                return view

    def wait_invite(self):
        deadline = time.monotonic() + self.timeout
        while not self.invitations:
            kind, code, _, body = self.receive(deadline)
            if kind != 2:
                raise ValueError("unexpected response while waiting for invitation")
            self.event(code, body)
        return self.invitations.popleft()
