#include "pr_account_wire.h"
#include "pr_room_wire.h"
#include <string.h>

size_t pr_page_encode(uint8_t *out, size_t capacity, uint16_t code, const pr_page_view *v) {
    if (v->count > PR_PAGE_LIMIT || (code != PR_LIST_PLAYERS && code != PR_GET_HISTORY)) return 0;
    pr_writer w;
    pr_writer_init(&w, out, capacity); pr_put_u16(&w, v->count);
    for (size_t i = 0; i < v->count; ++i) {
        if (code == PR_LIST_PLAYERS) {
            pr_put_u64(&w, v->players[i].user_id); pr_put_string(&w, v->players[i].name, strlen(v->players[i].name));
        } else {
            const pr_history_view *h = &v->history[i];
            pr_put_u64(&w, h->match_id); pr_put_u8(&w, h->state);
            pr_put_u64(&w, h->started_at); pr_put_u64(&w, h->ended_at);
            pr_put_u16(&w, h->total); pr_put_u8(&w, h->rank);
        }
    }
    pr_put_u64(&w, v->next_cursor);
    return w.ok ? w.used : 0;
}
int pr_page_decode(pr_page_view *v, uint16_t code, const uint8_t *payload, size_t n) {
    if (code != PR_LIST_PLAYERS && code != PR_GET_HISTORY) return 0;
    memset(v, 0, sizeof(*v));
    pr_reader r;
    pr_reader_init(&r, payload, n); v->count = pr_get_u16(&r);
    if (!r.ok || v->count > PR_PAGE_LIMIT) return 0;
    for (size_t i = 0; i < v->count; ++i) {
        if (code == PR_LIST_PLAYERS) {
            v->players[i].user_id = pr_get_u64(&r);
            size_t length;
            const uint8_t *name = pr_get_string(&r, &length, 96);
            if (!v->players[i].user_id || !name || !length || memchr(name, 0, length)) return 0;
            memcpy(v->players[i].name, name, length); v->players[i].name[length] = 0;
        } else {
            pr_history_view *h = &v->history[i];
            h->match_id = pr_get_u64(&r); h->state = pr_get_u8(&r);
            h->started_at = pr_get_u64(&r); h->ended_at = pr_get_u64(&r);
            h->total = pr_get_u16(&r); h->rank = pr_get_u8(&r);
            if (!h->match_id || (h->state != 2 && h->state != 3) || h->total > 5000 || h->rank > 4 ||
                (h->state == 2 ? !h->rank : h->rank != 0) || h->ended_at < h->started_at) return 0;
        }
    }
    v->next_cursor = pr_get_u64(&r);
    return pr_reader_done(&r);
}
size_t pr_stats_encode(uint8_t *out, size_t capacity, const pr_stats_view *v) {
    pr_writer w;
    pr_writer_init(&w, out, capacity);
    pr_put_u32(&w, v->finished); pr_put_u32(&w, v->wins); pr_put_u32(&w, v->average_x100); pr_put_u16(&w, v->best);
    return w.ok ? w.used : 0;
}
int pr_stats_decode(pr_stats_view *v, const uint8_t *payload, size_t n) {
    pr_reader r;
    pr_reader_init(&r, payload, n);
    v->finished = pr_get_u32(&r); v->wins = pr_get_u32(&r); v->average_x100 = pr_get_u32(&r); v->best = pr_get_u16(&r);
    return pr_reader_done(&r) && v->wins <= v->finished && v->average_x100 <= 500000 && v->best <= 5000;
}
size_t pr_invite_encode(uint8_t *out, size_t capacity, const pr_invite_view *v) {
    if (!v->inviter || !pr_valid_room_code(v->code) || !v->expires_ms || v->expires_ms > 60000) return 0;
    pr_writer w;
    pr_writer_init(&w, out, capacity);
    pr_put_u64(&w, v->inviter); pr_put_string(&w, v->code, 6); pr_put_u32(&w, v->expires_ms);
    return w.ok ? w.used : 0;
}
int pr_invite_decode(pr_invite_view *v, const uint8_t *payload, size_t n) {
    pr_reader r;
    pr_reader_init(&r, payload, n); v->inviter = pr_get_u64(&r);
    size_t length;
    const uint8_t *code = pr_get_string(&r, &length, 6);
    if (!code || length != 6 || memchr(code, 0, length)) return 0;
    memcpy(v->code, code, length); v->code[length] = 0;
    v->expires_ms = pr_get_u32(&r);
    return pr_reader_done(&r) && v->inviter && pr_valid_room_code(v->code) && v->expires_ms && v->expires_ms <= 60000;
}
