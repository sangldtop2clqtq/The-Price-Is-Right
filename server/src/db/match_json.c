#include "pr_persist.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define JSON_LIMIT 65536
typedef struct { char *data; size_t used; int ok; } json_writer;
uint64_t pr_wall_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void append(json_writer *w, const char *format, ...) {
    if (!w->ok) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(w->data + w->used, JSON_LIMIT - w->used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= JSON_LIMIT - w->used) { w->ok = 0; return; }
    w->used += (size_t)n;
}
static void text(json_writer *w, const char *value) {
    append(w, "\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') append(w, "\\%c", *p);
        else if (*p < 0x20) append(w, "\\u%04x", *p);
        else append(w, "%c", *p);
    }
    append(w, "\"");
}
char *pr_match_json(const pr_match *m) {
    if ((m->state != PR_FINISHED && m->state != PR_ABORTED) || !m->wall_started_ms || !m->wall_ended_ms) return NULL;
    char *data = malloc(JSON_LIMIT);
    if (!data) return NULL;
    json_writer w = {data, 0, 1};
    pr_game_view view;
    pr_game_public(m, 0, &view);
    append(&w, "{\"id\":%llu,\"room\":", (unsigned long long)m->id); text(&w, m->room_code);
    append(&w, ",\"status\":%u,\"abort_reason\":%u,\"started_ms\":%llu,\"ended_ms\":%llu,\"players\":[",
        m->state, m->abort_reason, (unsigned long long)m->wall_started_ms, (unsigned long long)m->wall_ended_ms);
    for (size_t i = 0; i < m->count; ++i) {
        append(&w, "%s{\"id\":%llu,\"name\":", i ? "," : "", (unsigned long long)m->participants[i].user_id);
        text(&w, m->participants[i].name);
        append(&w, ",\"total\":%u,\"rank\":%u,\"disconnected\":%s}", m->totals[i],
            view.rankings[i].rank, m->participants[i].connected ? "false" : "true");
    }
    append(&w, "],\"rounds\":[");
    for (int r = 0; r <= m->current; ++r) {
        const pr_game_round *round = &m->rounds[r];
        append(&w, "%s{\"no\":%d,\"type\":%u,\"state\":%u,\"duration_ms\":%u,\"actual_total\":%llu,\"items\":[",
            r ? "," : "", r + 1, round->public.type,
            r < m->current ? PR_REVEALED : m->phase, round->public.duration_ms,
            (unsigned long long)round->actual_total);
        for (size_t i = 0; i < round->public.item_count; ++i) {
            const pr_item *item = &round->public.items[i];
            append(&w, "%s{\"id\":%llu,\"name\":", i ? "," : "", (unsigned long long)item->product_id);
            text(&w, item->name); append(&w, ",\"image\":"); text(&w, item->image_key);
            append(&w, ",\"unit_price\":%llu,\"quantity\":%u}", (unsigned long long)round->unit_prices[i], item->quantity);
        }
        append(&w, "],\"submissions\":[");
        for (size_t i = 0; i < m->count; ++i) {
            const pr_score_view *s = &round->guesses[i];
            append(&w, "%s{\"id\":%llu,\"submitted\":%u,\"score\":%u,\"guess\":",
                i ? "," : "", (unsigned long long)s->user_id, s->submitted, s->score);
            if (s->submitted) append(&w, "%llu,\"response_ms\":%u}", (unsigned long long)s->guess, s->response_ms);
            else append(&w, "null,\"response_ms\":null}");
        }
        append(&w, "]}");
    }
    append(&w, "]}");
    if (!w.ok) { free(data); return NULL; }
    return data;
}
