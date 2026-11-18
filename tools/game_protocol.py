"""Decode public game events for demos and integration tests; no hidden catalogue prices."""
import struct


class Reader:
    def __init__(self, payload):
        self.payload, self.offset = payload, 0

    def number(self, fmt):
        size = struct.calcsize("!" + fmt)
        if self.offset + size > len(self.payload):
            raise ValueError("truncated game event")
        value, = struct.unpack_from("!" + fmt, self.payload, self.offset)
        self.offset += size
        return value

    def text(self):
        length = self.number("H")
        if not 1 <= length <= 96 or self.offset + length > len(self.payload):
            raise ValueError("invalid game string")
        value = self.payload[self.offset:self.offset + length].decode("utf-8")
        self.offset += length
        return value

    def count(self, maximum):
        count = self.number("H")
        if count > maximum:
            raise ValueError("invalid game list count")
        return count


def game_view(code, payload):
    r = Reader(payload)
    def participants():
        return [dict(id=r.number("Q"), name=r.text(), connected=r.number("B")) for _ in range(r.count(4))]
    def items():
        return [dict(id=r.number("Q"), name=r.text(), image_key=r.text(), quantity=r.number("H")) for _ in range(r.count(3))]
    def result():
        view = dict(match_id=r.number("Q"), round_no=r.number("B"), actual_total=r.number("Q"))
        scores = []
        for _ in range(r.count(4)):
            score = dict(id=r.number("Q"), submitted=r.number("B"))
            if score["submitted"]:
                score.update(guess=r.number("Q"), response_ms=r.number("I"))
            score.update(score=r.number("H"), total=r.number("H"))
            scores.append(score)
        view["scores"] = scores
        return view
    if code == 0x8203:
        view = result()
    else:
        view = dict(match_id=r.number("Q"))
        if code == 0x8206:
            view["status"] = r.number("H")
        elif code == 0x8201:
            view.update(participants=participants(), rounds=r.number("B"))
        elif code == 0x8204:
            view["state"] = r.number("B")
            view["rankings"] = [dict(id=r.number("Q"), total=r.number("H"), rank=r.number("B")) for _ in range(r.count(4))]
            view["abort_reason"] = r.number("B")
        elif code in (0x8202, 0x8205):
            if code == 0x8205:
                view.update(state=r.number("B"), participants=participants())
            view["round_no"] = r.number("B")
            if code == 0x8205:
                view["phase"] = r.number("B")
            view.update(type=r.number("B"), items=items(), duration_ms=r.number("I"), remaining_ms=r.number("I"))
            if code == 0x8205:
                view["public_scores"] = [dict(id=r.number("Q"), total=r.number("H")) for _ in range(r.count(4))]
                view["has_result"] = r.number("B")
                if view["has_result"]:
                    view["result"] = result()
        else:
            raise ValueError("unknown game event")
    if r.offset != len(payload):
        raise ValueError("trailing game event bytes")
    return view
