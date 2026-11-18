"""Public account/history/invitation payload decoders for diagnostics."""
from game_protocol import Reader


def page_view(code, payload):
    if code not in (0x105, 0x301):
        raise ValueError("unsupported page")
    r = Reader(payload)
    count = r.count(20)
    if code == 0x105:
        rows = [dict(id=r.number("Q"), name=r.text()) for _ in range(count)]
    else:
        rows = [dict(match_id=r.number("Q"), state=r.number("B"), started_ms=r.number("Q"),
                     ended_ms=r.number("Q"), total=r.number("H"), rank=r.number("B")) for _ in range(count)]
    next_cursor = r.number("Q")
    if r.offset != len(payload):
        raise ValueError("trailing page bytes")
    return dict(rows=rows, next_cursor=next_cursor)


def stats_view(payload):
    r = Reader(payload)
    view = dict(finished=r.number("I"), wins=r.number("I"), average_x100=r.number("I"), best=r.number("H"))
    if r.offset != len(payload):
        raise ValueError("trailing stats bytes")
    return view


def invite_view(payload):
    r = Reader(payload)
    view = dict(inviter=r.number("Q"), code=r.text(), expires_ms=r.number("I"))
    if r.offset != len(payload) or len(view["code"]) != 6 or not 0 < view["expires_ms"] <= 60000:
        raise ValueError("invalid invitation")
    return view
