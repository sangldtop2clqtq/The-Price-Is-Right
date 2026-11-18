#include "pr_protocol.h"
#include <string.h>

void pr_wipe(void *memory, size_t length) {
    volatile uint8_t *bytes = memory;
    while (length--) *bytes++ = 0;
}

void pr_writer_init(pr_writer *w, uint8_t *p, size_t n) {
    *w = (pr_writer){p, n, 0, 1};
}
static void put(pr_writer *w, uint64_t value, size_t n) {
    if (!w->ok || n > w->capacity - w->used) { w->ok = 0; return; }
    for (size_t i = n; i > 0; --i)
        w->data[w->used++] = (uint8_t)(value >> ((i - 1) * 8));
}
void pr_put_u8(pr_writer *w, uint8_t v) { put(w, v, 1); }
void pr_put_u16(pr_writer *w, uint16_t v) { put(w, v, 2); }
void pr_put_u32(pr_writer *w, uint32_t v) { put(w, v, 4); }
void pr_put_u64(pr_writer *w, uint64_t v) { put(w, v, 8); }
void pr_put_string(pr_writer *w, const char *s, size_t n) {
    if (n > UINT16_MAX || (n && !s)) { w->ok = 0; return; }
    pr_put_u16(w, (uint16_t)n);
    if (!w->ok || n > w->capacity - w->used) { w->ok = 0; return; }
    if (n) memcpy(w->data + w->used, s, n);
    w->used += n;
}
void pr_reader_init(pr_reader *r, const uint8_t *p, size_t n) {
    *r = (pr_reader){p, n, 0, 1};
}
static uint64_t get(pr_reader *r, size_t n) {
    uint64_t value = 0;
    if (!r->ok || n > r->length - r->used) { r->ok = 0; return 0; }
    for (size_t i = 0; i < n; ++i) value = (value << 8) | r->data[r->used++];
    return value;
}
uint8_t pr_get_u8(pr_reader *r) { return (uint8_t)get(r, 1); }
uint16_t pr_get_u16(pr_reader *r) { return (uint16_t)get(r, 2); }
uint32_t pr_get_u32(pr_reader *r) { return (uint32_t)get(r, 4); }
uint64_t pr_get_u64(pr_reader *r) { return get(r, 8); }
const uint8_t *pr_get_string(pr_reader *r, size_t *length, size_t limit) {
    size_t n = pr_get_u16(r);
    *length = 0;
    if (!r->ok || n > limit || n > r->length - r->used) { r->ok = 0; return NULL; }
    const uint8_t *result = r->data + r->used;
    r->used += n;
    *length = n;
    return result;
}
int pr_reader_done(const pr_reader *r) { return r->ok && r->used == r->length; }

static int valid_header(const pr_header *h) {
    return h->kind <= PR_EVENT && h->payload_length <= PR_MAX_PAYLOAD &&
           ((h->kind == PR_EVENT) ? h->request_id == 0 : h->request_id != 0);
}
int pr_decode_header(const uint8_t *bytes, pr_header *h) {
    pr_reader r;
    pr_reader_init(&r, bytes, PR_HEADER_SIZE);
    uint16_t magic = pr_get_u16(&r);
    uint8_t version = pr_get_u8(&r);
    h->kind = pr_get_u8(&r);
    h->code = pr_get_u16(&r);
    h->payload_length = pr_get_u16(&r);
    h->request_id = pr_get_u32(&r);
    return magic == PR_MAGIC && version == PR_VERSION && valid_header(h);
}
size_t pr_encode_frame(uint8_t *out, size_t capacity, const pr_header *h,
                       const uint8_t *payload) {
    size_t total = PR_HEADER_SIZE + h->payload_length;
    if (!valid_header(h) || capacity < total || (h->payload_length && !payload)) return 0;
    pr_writer w;
    pr_writer_init(&w, out, capacity);
    pr_put_u16(&w, PR_MAGIC);
    pr_put_u8(&w, PR_VERSION);
    pr_put_u8(&w, h->kind);
    pr_put_u16(&w, h->code);
    pr_put_u16(&w, h->payload_length);
    pr_put_u32(&w, h->request_id);
    if (h->payload_length) memcpy(out + PR_HEADER_SIZE, payload, h->payload_length);
    return total;
}
void pr_parser_init(pr_parser *p) { p->used = 0; }
int pr_parser_feed(pr_parser *p, const uint8_t *bytes, size_t n,
                   pr_frame_fn callback, void *context) {
    while (n) {
        size_t target = PR_HEADER_SIZE;
        pr_header h;
        if (p->used >= PR_HEADER_SIZE) {
            if (!pr_decode_header(p->bytes, &h)) return -1;
            target += h.payload_length;
        }
        size_t take = target - p->used;
        if (take > n) take = n;
        memcpy(p->bytes + p->used, bytes, take);
        p->used += take;
        bytes += take;
        n -= take;
        if (p->used < PR_HEADER_SIZE) continue;
        if (!pr_decode_header(p->bytes, &h)) return -1;
        if (p->used == PR_HEADER_SIZE + h.payload_length) {
            if (callback(context, &h, p->bytes + PR_HEADER_SIZE) != 0) return -1;
            p->used = 0;
        }
    }
    return 0;
}
int pr_is_request_code(uint16_t code) {
    switch (code) {
    case PR_REGISTER: case PR_LOGIN: case PR_LOGOUT: case PR_PING:
    case PR_CREATE_ROOM: case PR_JOIN_ROOM: case PR_LEAVE_ROOM: case PR_SET_READY:
    case PR_LIST_PLAYERS: case PR_INVITE_PLAYER: case PR_START_GAME:
    case PR_SUBMIT_GUESS: case PR_GET_HISTORY: case PR_GET_STATS: return 1;
    default: return 0;
    }
}
