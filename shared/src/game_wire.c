#include "pr_game_wire.h"
#include <string.h>

static void participants_out(pr_writer *w, const pr_game_view *v) {
    pr_put_u16(w, v->count);
    for (size_t i = 0; i < v->count; ++i) {
        pr_put_u64(w, v->participants[i].user_id);
        pr_put_string(w, v->participants[i].name, strlen(v->participants[i].name));
        pr_put_u8(w, v->participants[i].connected);
    }
}
static void items_out(pr_writer *w, const pr_game_view *v) {
    pr_put_u16(w, v->item_count);
    for (size_t i = 0; i < v->item_count; ++i) {
        const pr_item *p = &v->items[i];
        pr_put_u64(w, p->product_id); pr_put_string(w, p->name, strlen(p->name));
        pr_put_string(w, p->image_key, strlen(p->image_key)); pr_put_u16(w, p->quantity);
    }
}
static void result_out(pr_writer *w, const pr_result_view *v) {
    pr_put_u64(w, v->match_id); pr_put_u8(w, v->round_no); pr_put_u64(w, v->actual_total);
    pr_put_u16(w, v->count);
    for (size_t i = 0; i < v->count; ++i) {
        const pr_score_view *s = &v->scores[i];
        pr_put_u64(w, s->user_id); pr_put_u8(w, s->submitted);
        if (s->submitted) { pr_put_u64(w, s->guess); pr_put_u32(w, s->response_ms); }
        pr_put_u16(w, s->score); pr_put_u16(w, s->total);
    }
}
size_t pr_game_encode(uint8_t *out, size_t capacity, uint16_t code, const pr_game_view *v) {
    if (!v->match_id || v->count < 2 || v->count > PR_MAX_PLAYERS || v->item_count > PR_MAX_ITEMS ||
        v->result.count > PR_MAX_PLAYERS) return 0;
    pr_writer w;
    pr_writer_init(&w, out, capacity);
    if (code == PR_GAME_SAVE_FAILED) {
        pr_put_u64(&w, v->match_id); pr_put_u16(&w, v->save_status);
        return w.ok ? w.used : 0;
    }
    if (code == PR_ROUND_RESULT) result_out(&w, &v->result);
    else {
        pr_put_u64(&w, v->match_id);
        if (code == PR_GAME_STARTED) { participants_out(&w, v); pr_put_u8(&w, PR_ROUNDS); }
        else if (code == PR_GAME_OVER) {
            pr_put_u8(&w, v->state); pr_put_u16(&w, v->count);
            for (size_t i = 0; i < v->count; ++i) {
                pr_put_u64(&w, v->rankings[i].user_id); pr_put_u16(&w, v->rankings[i].total);
                pr_put_u8(&w, v->rankings[i].rank);
            }
            pr_put_u8(&w, v->abort_reason);
        } else if (code == PR_NEW_ROUND || code == PR_GAME_SNAPSHOT) {
            if (code == PR_GAME_SNAPSHOT) { pr_put_u8(&w, v->state); participants_out(&w, v); }
            pr_put_u8(&w, v->round_no);
            if (code == PR_GAME_SNAPSHOT) pr_put_u8(&w, v->round_state);
            pr_put_u8(&w, v->type); items_out(&w, v);
            pr_put_u32(&w, v->duration_ms); pr_put_u32(&w, v->remaining_ms);
            if (code == PR_GAME_SNAPSHOT) {
                pr_put_u16(&w, v->count);
                for (size_t i = 0; i < v->count; ++i) {
                    pr_put_u64(&w, v->participants[i].user_id); pr_put_u16(&w, v->totals[i]);
                }
                pr_put_u8(&w, v->has_result);
                if (v->has_result) result_out(&w, &v->result);
            }
        } else return 0;
    }
    return w.ok ? w.used : 0;
}
static int text(pr_reader *r, char *out, size_t limit) {
    size_t n;
    const uint8_t *s = pr_get_string(r, &n, limit);
    if (!s || !n || memchr(s, 0, n)) return 0;
    memcpy(out, s, n); out[n] = 0; return 1;
}
static int participants_in(pr_reader *r, pr_game_view *v) {
    v->count = pr_get_u16(r);
    if (!r->ok || v->count < 2 || v->count > PR_MAX_PLAYERS) return 0;
    for (size_t i = 0; i < v->count; ++i) {
        pr_participant *p = &v->participants[i];
        p->user_id = pr_get_u64(r);
        if (!text(r, p->name, 96)) return 0;
        p->connected = pr_get_u8(r);
        if (!p->user_id || p->connected > 1) return 0;
        for (size_t j = 0; j < i; ++j) if (v->participants[j].user_id == p->user_id) return 0;
    }
    return r->ok;
}
static int items_in(pr_reader *r, pr_game_view *v) {
    v->item_count = pr_get_u16(r);
    if (!r->ok || v->item_count > PR_MAX_ITEMS) return 0;
    for (size_t i = 0; i < v->item_count; ++i) {
        pr_item *p = &v->items[i];
        p->product_id = pr_get_u64(r);
        if (!text(r, p->name, 96) || !text(r, p->image_key, 64)) return 0;
        p->quantity = pr_get_u16(r);
        if (!p->product_id || !p->quantity || p->quantity > 10) return 0;
    }
    return r->ok;
}
static int result_in(pr_reader *r, pr_result_view *v) {
    v->match_id = pr_get_u64(r); v->round_no = pr_get_u8(r); v->actual_total = pr_get_u64(r);
    v->count = pr_get_u16(r);
    if (!r->ok || !v->match_id || !v->round_no || v->round_no > PR_ROUNDS ||
        !v->actual_total || v->actual_total > UINT64_C(1000000000000) || v->count < 2 || v->count > 4) return 0;
    for (size_t i = 0; i < v->count; ++i) {
        pr_score_view *s = &v->scores[i];
        s->user_id = pr_get_u64(r); s->submitted = pr_get_u8(r);
        if (!s->user_id || s->submitted > 1) return 0;
        if (s->submitted) { s->guess = pr_get_u64(r); s->response_ms = pr_get_u32(r); }
        s->score = pr_get_u16(r); s->total = pr_get_u16(r);
        if (s->guess > UINT64_C(1000000000000) || s->score > 1000 || s->total > 5000 ||
            (!s->submitted && s->score)) return 0;
        for (size_t j = 0; j < i; ++j) if (v->scores[j].user_id == s->user_id) return 0;
    }
    return r->ok;
}
int pr_game_decode(pr_game_view *v, uint16_t code, const uint8_t *bytes, size_t length) {
    memset(v, 0, sizeof(*v));
    pr_reader r;
    pr_reader_init(&r, bytes, length);
    if (code == PR_GAME_SAVE_FAILED) {
        v->match_id = pr_get_u64(&r); v->save_status = pr_get_u16(&r);
        return pr_reader_done(&r) && v->match_id && v->save_status == PR_INTERNAL_ERROR;
    }
    if (code == PR_ROUND_RESULT) {
        if (!result_in(&r, &v->result)) return 0;
        v->match_id = v->result.match_id; v->round_no = v->result.round_no; v->count = v->result.count;
    } else {
        v->match_id = pr_get_u64(&r);
        if (!v->match_id) return 0;
        if (code == PR_GAME_STARTED) {
            if (!participants_in(&r, v) || pr_get_u8(&r) != PR_ROUNDS) return 0;
        } else if (code == PR_GAME_OVER) {
            v->state = pr_get_u8(&r); v->count = pr_get_u16(&r);
            if ((v->state != PR_FINISHED && v->state != PR_ABORTED) || v->count < 2 || v->count > 4) return 0;
            for (size_t i = 0; i < v->count; ++i) {
                pr_ranking *p = &v->rankings[i];
                p->user_id = pr_get_u64(&r); p->total = pr_get_u16(&r); p->rank = pr_get_u8(&r);
                if (!p->user_id || p->total > 5000 || p->rank > v->count ||
                    (v->state == PR_ABORTED ? p->rank != 0 : p->rank == 0)) return 0;
                for (size_t j = 0; j < i; ++j) if (v->rankings[j].user_id == p->user_id) return 0;
            }
            v->abort_reason = pr_get_u8(&r);
            if (v->abort_reason > PR_ABORT_SERVER || (v->state == PR_FINISHED ? v->abort_reason != 0 : v->abort_reason == 0)) return 0;
        } else if (code == PR_NEW_ROUND || code == PR_GAME_SNAPSHOT) {
            if (code == PR_GAME_SNAPSHOT) {
                v->state = pr_get_u8(&r);
                if (v->state > PR_ABORTED || !participants_in(&r, v)) return 0;
            }
            v->round_no = pr_get_u8(&r);
            v->round_state = code == PR_GAME_SNAPSHOT ? pr_get_u8(&r) : PR_ACCEPTING;
            v->type = pr_get_u8(&r);
            if (v->round_no > PR_ROUNDS || v->round_state > PR_REVEALED || v->type > PR_BASKET || !items_in(&r, v)) return 0;
            v->duration_ms = pr_get_u32(&r); v->remaining_ms = pr_get_u32(&r);
            if (v->remaining_ms > v->duration_ms || (v->round_no ?
                (!v->duration_ms || (v->type == PR_SINGLE ? v->item_count != 1 : v->item_count < 2)) :
                (code != PR_GAME_SNAPSHOT || v->state != PR_PREPARING || v->item_count || v->duration_ms))) return 0;
            if (code == PR_GAME_SNAPSHOT) {
                uint16_t count = pr_get_u16(&r);
                if (count != v->count) return 0;
                for (size_t i = 0; i < count; ++i) {
                    if (pr_get_u64(&r) != v->participants[i].user_id) return 0;
                    v->totals[i] = pr_get_u16(&r);
                    if (v->totals[i] > 5000) return 0;
                }
                v->has_result = pr_get_u8(&r);
                if (v->has_result > 1 || (v->has_result &&
                    (v->round_state != PR_REVEALED || !result_in(&r, &v->result) ||
                     v->result.match_id != v->match_id || v->result.round_no != v->round_no))) return 0;
            }
        } else return 0;
    }
    return pr_reader_done(&r);
}
