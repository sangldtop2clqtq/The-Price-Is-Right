#include "pr_server.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pr_app *app;
    size_t slot;
    uint64_t generation, user_id;
    uint16_t limit;
    pr_header header;
} account_task;
static int numeric(const char *text, uint64_t limit, uint64_t *out) {
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (!*text || *end || errno || value > limit) return 0;
    *out = (uint64_t)value; return 1;
}
static pr_client *owner(account_task *task) {
    pr_client *c = &task->app->clients[task->slot];
    return c->fd >= 0 && c->generation == task->generation && c->user_id == task->user_id ? c : NULL;
}
static void done(account_task *task, uint16_t status, const uint8_t *body, size_t n) {
    pr_client *c = owner(task);
    if (c) {
        c->auth_busy = 0;
        if (pr_reply(c, &task->header, status, body, n) < 0) pr_close_client(c);
    }
    free(task);
}
static void history_ready(void *context, PGresult *result) {
    account_task *task = context;
    if (!owner(task)) { free(task); return; }
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) > task->limit + 1) {
        done(task, PR_INTERNAL_ERROR, NULL, 0); return;
    }
    pr_page_view page;
    memset(&page, 0, sizeof(page));
    int rows = PQntuples(result);
    page.count = (uint16_t)(rows > task->limit ? task->limit : rows);
    for (size_t i = 0; i < page.count; ++i) {
        uint64_t values[7];
        for (int j = 0; j < 7; ++j) {
            if (!numeric(PQgetvalue(result, (int)i, j), INT64_MAX, &values[j])) {
                done(task, PR_INTERNAL_ERROR, NULL, 0); return;
            }
        }
        if (!values[0] || values[1] < 2 || values[1] > 3 || values[4] > 5000 || values[5] > 4) {
            done(task, PR_INTERNAL_ERROR, NULL, 0); return;
        }
        page.history[i] = (pr_history_view){values[0], values[2], values[3], (uint8_t)values[1], (uint8_t)values[5], (uint16_t)values[4]};
        if (i + 1 == page.count && rows > task->limit) page.next_cursor = values[6];
    }
    uint8_t body[PR_MAX_PAYLOAD - 2];
    size_t n = pr_page_encode(body, sizeof(body), PR_GET_HISTORY, &page);
    done(task, n ? PR_OK : PR_INTERNAL_ERROR, body, n);
}
static void stats_ready(void *context, PGresult *result) {
    account_task *task = context;
    if (!owner(task)) { free(task); return; }
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) != 1) {
        done(task, PR_INTERNAL_ERROR, NULL, 0); return;
    }
    uint64_t values[4];
    for (int i = 0; i < 4; ++i) if (!numeric(PQgetvalue(result, 0, i), UINT32_MAX, &values[i])) {
        done(task, PR_INTERNAL_ERROR, NULL, 0); return;
    }
    if (values[1] > values[0] || values[2] > 500000 || values[3] > 5000) {
        done(task, PR_INTERNAL_ERROR, NULL, 0); return;
    }
    pr_stats_view stats = {(uint32_t)values[0], (uint32_t)values[1], (uint32_t)values[2], (uint16_t)values[3]};
    uint8_t body[14];
    size_t n = pr_stats_encode(body, sizeof(body), &stats);
    done(task, n ? PR_OK : PR_INTERNAL_ERROR, body, n);
}
static int available(const pr_client *c) {
    return c->fd >= 0 && c->user_id && !c->drop && !c->read_closed && c->room_index < 0;
}
void pr_social_tick(pr_app *app, double now) {
    for (size_t i = 0; i < 128; ++i)
        if (app->invitations[i].expires <= now) memset(&app->invitations[i], 0, sizeof(pr_invitation));
}
void pr_social_detach(pr_client *c) {
    if (!c->app) return;
    for (size_t i = 0; i < 128; ++i) {
        pr_invitation *v = &c->app->invitations[i];
        if (v->inviter_generation == c->generation || v->target_generation == c->generation) memset(v, 0, sizeof(*v));
    }
}
static int online_list(pr_app *app, pr_client *c, const pr_header *h, uint64_t cursor, uint16_t limit) {
    pr_page_view page;
    memset(&page, 0, sizeof(page));
    uint64_t after = cursor;
    for (size_t n = 0; n <= limit; ++n) {
        pr_client *next = NULL;
        for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
            pr_client *candidate = &app->clients[i];
            if (candidate != c && available(candidate) && candidate->user_id > after &&
                (!next || candidate->user_id < next->user_id)) next = candidate;
        }
        if (!next) break;
        if (n == limit) { page.next_cursor = after; break; }
        pr_online_view *p = &page.players[page.count++];
        p->user_id = next->user_id; strcpy(p->name, next->display_name); after = next->user_id;
    }
    uint8_t body[PR_MAX_PAYLOAD - 2];
    size_t n = pr_page_encode(body, sizeof(body), PR_LIST_PLAYERS, &page);
    return pr_reply(c, h, n ? PR_OK : PR_INTERNAL_ERROR, body, n);
}
static int invite(pr_app *app, pr_client *sender, const pr_header *h, uint64_t target_id) {
    int room = sender->room_index;
    int member = room < 0 ? -1 : pr_room_find_member(&app->rooms[room], sender->user_id);
    if (member < 0 || app->rooms[room].members[member].role != PR_PLAYER)
        return pr_reply(sender, h, PR_WRONG_STATE, NULL, 0);
    pr_client *target = NULL;
    for (size_t i = 0; i < PR_MAX_CLIENTS; ++i)
        if (&app->clients[i] != sender && available(&app->clients[i]) && app->clients[i].user_id == target_id) target = &app->clients[i];
    if (!target) return pr_reply(sender, h, PR_PLAYER_UNAVAILABLE, NULL, 0);
    pr_social_tick(app, pr_server_now());
    pr_invitation *free_slot = NULL;
    for (size_t i = 0; i < 128; ++i) {
        pr_invitation *v = &app->invitations[i];
        if (!v->expires) { free_slot = v; continue; }
        if (v->inviter_generation == sender->generation && v->target_generation == target->generation &&
            !strcmp(v->code, app->rooms[room].code)) return pr_reply(sender, h, PR_PLAYER_UNAVAILABLE, NULL, 0);
    }
    if (!free_slot) return pr_reply(sender, h, PR_INTERNAL_ERROR, NULL, 0);
    pr_invite_view view = {sender->user_id, "", app->invite_ttl_ms};
    strcpy(view.code, app->rooms[room].code);
    uint8_t body[32];
    size_t n = pr_invite_encode(body, sizeof(body), &view);
    pr_header event = {PR_EVENT, PR_INVITE_RECEIVED, (uint16_t)n, 0};
    if (!n || pr_tx_frame(&target->tx, &event, body) < 0) {
        target->drop = 1; target->close_reason = "tx_queue_limit";
        return pr_reply(sender, h, PR_PLAYER_UNAVAILABLE, NULL, 0);
    }
    free_slot->inviter_generation = sender->generation; free_slot->target_generation = target->generation;
    free_slot->expires = pr_server_now() + (double)app->invite_ttl_ms / 1000.0; strcpy(free_slot->code, view.code);
    return pr_reply(sender, h, PR_OK, NULL, 0);
}
int pr_account_handle(pr_app *app, size_t slot, const pr_header *h, const uint8_t *payload) {
    pr_client *c = &app->clients[slot];
    if (!c->user_id) return pr_reply(c, h, PR_NOT_AUTHENTICATED, NULL, 0);
    pr_reader r;
    pr_reader_init(&r, payload, h->payload_length);
    if (h->code == PR_INVITE_PLAYER) {
        uint64_t target = pr_get_u64(&r);
        if (!pr_reader_done(&r) || !target || target > INT64_MAX) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
        return invite(app, c, h, target);
    }
    uint64_t cursor = 0;
    uint16_t limit = 0;
    if (h->code == PR_GET_HISTORY || h->code == PR_LIST_PLAYERS) {
        cursor = pr_get_u64(&r); limit = pr_get_u16(&r);
        if (!pr_reader_done(&r) || cursor > INT64_MAX || !limit || limit > PR_PAGE_LIMIT)
            return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    } else if (h->payload_length) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    if (h->code == PR_LIST_PLAYERS) return online_list(app, c, h, cursor, limit);
    account_task *task = calloc(1, sizeof(*task));
    if (!task) return pr_reply(c, h, PR_INTERNAL_ERROR, NULL, 0);
    *task = (account_task){app, slot, c->generation, c->user_id, limit, *h};
    char user[32], cursor_text[32], limit_text[8];
    snprintf(user, sizeof(user), "%llu", (unsigned long long)c->user_id);
    snprintf(cursor_text, sizeof(cursor_text), "%llu", (unsigned long long)cursor);
    snprintf(limit_text, sizeof(limit_text), "%u", limit + 1);
    const char *params[] = {user, cursor_text, limit_text};
    c->auth_busy = 1;
    int submitted;
    if (h->code == PR_GET_HISTORY)
        submitted = pr_db_submit(app->db,
            "SELECT m.id,m.status,(extract(epoch from m.started_at)*1000)::bigint,"
            "(extract(epoch from m.ended_at)*1000)::bigint,mp.total_score,mp.rank,m.history_seq "
            "FROM matches m JOIN match_players mp ON mp.match_id=m.id WHERE mp.user_id=$1::bigint "
            "AND ($2::bigint=0 OR m.history_seq<$2::bigint) ORDER BY m.history_seq DESC LIMIT $3::integer",
            3, params, history_ready, task);
    else
        submitted = pr_db_submit(app->db,
            "SELECT count(*),count(*) FILTER(WHERE mp.rank=1),COALESCE(round(avg(mp.total_score)*100),0),"
            "COALESCE(max(mp.total_score),0) FROM match_players mp JOIN matches m ON m.id=mp.match_id "
            "WHERE mp.user_id=$1::bigint AND m.status=2", 1, params, stats_ready, task);
    if (submitted < 0) done(task, PR_INTERNAL_ERROR, NULL, 0);
    return 0;
}
