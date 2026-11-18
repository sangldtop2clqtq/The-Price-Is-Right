#include "pr_room_wire.h"
#include <string.h>
int pr_valid_room_code(const char *code) {
    if (strlen(code) != 6) return 0;
    for (size_t i = 0; i < 6; ++i)
        if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9'))) return 0;
    return 1;
}
static int valid(const pr_room_view *v) {
    if (!pr_valid_room_code(v->code) || v->state > PR_IN_GAME || v->self_role > PR_SPECTATOR ||
        !v->host_id || !v->revision || v->player_count > PR_MAX_PLAYERS ||
        (v->state == PR_WAITING && !v->player_count)) return 0;
    int found_host = 0;
    for (size_t i = 0; i < v->player_count; ++i) {
        const pr_player_view *p = &v->players[i];
        size_t n = strlen(p->name);
        if (!p->user_id || !n || n > 96 || p->ready > 1 || p->connected > 1) return 0;
        if (p->user_id == v->host_id) found_host = 1;
        for (size_t j = 0; j < i; ++j) if (v->players[j].user_id == p->user_id) return 0;
    }
    return v->state == PR_IN_GAME || found_host;
}
size_t pr_room_encode(uint8_t *out, size_t capacity, const pr_room_view *v) {
    if (!valid(v)) return 0;
    pr_writer w;
    pr_writer_init(&w, out, capacity);
    pr_put_string(&w, v->code, 6);
    pr_put_u8(&w, v->state); pr_put_u64(&w, v->host_id); pr_put_u32(&w, v->revision);
    pr_put_u8(&w, v->self_role); pr_put_u32(&w, v->spectator_count);
    pr_put_u16(&w, v->player_count);
    for (size_t i = 0; i < v->player_count; ++i) {
        const pr_player_view *p = &v->players[i];
        pr_put_u64(&w, p->user_id); pr_put_string(&w, p->name, strlen(p->name));
        pr_put_u8(&w, p->ready); pr_put_u8(&w, p->connected);
    }
    return w.ok ? w.used : 0;
}
static int read_string(pr_reader *r, char *out, size_t limit) {
    size_t n;
    const uint8_t *p = pr_get_string(r, &n, limit);
    if (!p || memchr(p, 0, n)) return 0;
    memcpy(out, p, n); out[n] = 0; return 1;
}
int pr_room_decode(pr_room_view *v, const uint8_t *bytes, size_t length) {
    memset(v, 0, sizeof(*v));
    pr_reader r;
    pr_reader_init(&r, bytes, length);
    if (!read_string(&r, v->code, 6)) return 0;
    v->state = pr_get_u8(&r); v->host_id = pr_get_u64(&r); v->revision = pr_get_u32(&r);
    v->self_role = pr_get_u8(&r); v->spectator_count = pr_get_u32(&r);
    v->player_count = pr_get_u16(&r);
    if (!r.ok || v->player_count > PR_MAX_PLAYERS) return 0;
    for (size_t i = 0; i < v->player_count; ++i) {
        pr_player_view *p = &v->players[i];
        p->user_id = pr_get_u64(&r);
        if (!read_string(&r, p->name, 96)) return 0;
        p->ready = pr_get_u8(&r); p->connected = pr_get_u8(&r);
    }
    return pr_reader_done(&r) && valid(v);
}
