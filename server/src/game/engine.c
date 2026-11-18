#include "pr_server.h"
#include "pr_credentials.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct { pr_app *app; int room; uint64_t id; } preparation;

static int send_view(pr_client *c, uint16_t code, const pr_game_view *v) {
    uint8_t payload[PR_MAX_PAYLOAD];
    size_t n = pr_game_encode(payload, sizeof(payload), code, v);
    pr_header h = {PR_EVENT, code, (uint16_t)n, 0};
    if (!n || pr_tx_frame(&c->tx, &h, payload) < 0) {
        c->drop = 1; c->close_reason = "tx_queue_limit"; return -1;
    }
    return 0;
}
static void event(pr_app *app, int room, uint16_t code) {
    if (app->shutting_down || !app->matches[room]) return;
    pr_game_view view;
    pr_game_public(app->matches[room], pr_server_now(), &view);
    if (code == PR_GAME_SAVE_FAILED) view.save_status = PR_INTERNAL_ERROR;
    for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
        pr_client *c = &app->clients[i];
        if (c->fd >= 0 && !c->drop && c->room_index == room) send_view(c, code, &view);
    }
    pr_log_match("game_event", app->rooms[room].code, view.match_id, view.round_no, code);
}
static void finish(pr_app *app, int room) {
    if (pr_persist_enqueue(app, room) < 0)
        pr_game_persisted(app, room, app->matches[room]->id, 0);
}
void pr_game_persisted(pr_app *app, int room, uint64_t id, int success) {
    pr_match *m = app->matches[room];
    if (!m || m->id != id) return;
    m->save_pending = 0; m->persisted = success; m->save_failed = !success;
    if (app->rooms[room].state != PR_IN_GAME || strcmp(app->rooms[room].code, m->room_code)) return;
    event(app, room, success ? PR_GAME_OVER : PR_GAME_SAVE_FAILED);
    pr_room_finish(&app->rooms[room]);
    pr_room_broadcast(app, room);
}
static uint64_t number(const char *text) {
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    return !*text || *end || errno || !value || value > INT64_MAX ? 0 : (uint64_t)value;
}
static int product_row(PGresult *result, int row, pr_product *p) {
    p->item.product_id = number(PQgetvalue(result, row, 0));
    const char *name = PQgetvalue(result, row, 1), *image = PQgetvalue(result, row, 2);
    size_t name_n = strlen(name), image_n = strlen(image);
    p->price = number(PQgetvalue(result, row, 3));
    if (!p->item.product_id || !p->price || p->price > PR_MAX_PRICE || !name_n || name_n > 96 ||
        !pr_valid_utf8((const uint8_t *)name, name_n) || !image_n || image_n > 64 ||
        image[0] == '/' || strstr(image, "..")) return 0;
    for (size_t i = 0; i < image_n; ++i) {
        char c = image[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '_' || c == '-' || c == '.')) return 0;
    }
    strcpy(p->item.name, name); strcpy(p->item.image_key, image); p->item.quantity = 1;
    return 1;
}
static void prepared(void *context, PGresult *result) {
    preparation *task = context;
    pr_app *app = task->app;
    int index = task->room;
    pr_match *m = app->matches[index];
    uint64_t id = task->id;
    free(task);
    if (app->shutting_down || !m || m->id != id || m->state != PR_PREPARING) return;
    int count = result && PQresultStatus(result) == PGRES_TUPLES_OK ? PQntuples(result) : 0;
    pr_product *products = count >= 3 && count <= PR_MAX_PRODUCTS ? calloc((size_t)count, sizeof(*products)) : NULL;
    int valid = products != NULL;
    for (int i = 0; valid && i < count; ++i) valid = product_row(result, i, &products[i]);
    if (valid) valid = pr_game_prepare(m, products, (size_t)count, randombytes_uniform(UINT32_MAX), pr_server_now());
    free(products);
    if (!valid) {
        pr_game_abort(m, PR_ABORT_SERVER); finish(app, index); return;
    }
    event(app, index, PR_GAME_STARTED);
    event(app, index, PR_NEW_ROUND);
}
int pr_game_start(pr_app *app, size_t slot, const pr_header *h) {
    pr_client *c = &app->clients[slot];
    int index = c->room_index;
    uint16_t status = index < 0 ? PR_WRONG_STATE : pr_room_can_start(&app->rooms[index], c->user_id);
    if (status != PR_OK) return pr_reply(c, h, status, NULL, 0);
    pr_match *m = calloc(1, sizeof(*m));
    preparation *task = calloc(1, sizeof(*task));
    if (!m || !task) { free(m); free(task); return pr_reply(c, h, PR_INTERNAL_ERROR, NULL, 0); }
    uint64_t id;
    int collision;
    do {
        randombytes_buf(&id, sizeof(id)); id &= INT64_MAX; collision = !id;
        for (size_t i = 0; i < PR_MAX_ROOMS; ++i) if (app->matches[i] && app->matches[i]->id == id) collision = 1;
    } while (collision);
    pr_room_view view;
    pr_room_snapshot(&app->rooms[index], c->user_id, &view);
    pr_game_init(m, id, &view, app->single_ms, app->basket_ms, app->reveal_ms);
    strcpy(m->room_code, view.code); m->wall_started_ms = pr_wall_ms();
    task->app = app; task->room = index; task->id = id;
    if (pr_db_submit(app->db, "SELECT id,name,image_key,price_vnd FROM products WHERE active ORDER BY id LIMIT 300",
                     0, NULL, prepared, task) < 0) {
        free(m); free(task); return pr_reply(c, h, PR_INTERNAL_ERROR, NULL, 0);
    }
    free(app->matches[index]); app->matches[index] = m;
    app->rooms[index].state = PR_IN_GAME;
    if (app->rooms[index].revision < UINT32_MAX) ++app->rooms[index].revision;
    uint8_t body[8];
    pr_writer w;
    pr_writer_init(&w, body, sizeof(body)); pr_put_u64(&w, id);
    int result = pr_reply(c, h, PR_OK, body, sizeof(body));
    pr_room_broadcast(app, index);
    return result;
}
int pr_game_guess(pr_app *app, size_t slot, const pr_header *h, const uint8_t *payload) {
    pr_client *c = &app->clients[slot];
    pr_reader r;
    pr_reader_init(&r, payload, h->payload_length);
    uint64_t id = pr_get_u64(&r);
    uint8_t round_no = pr_get_u8(&r);
    uint64_t amount = pr_get_u64(&r);
    if (!pr_reader_done(&r) || !id || round_no < 1 || round_no > PR_ROUNDS || amount > PR_MAX_PRICE)
        return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    pr_game_tick_all(app, pr_server_now());
    int room = c->room_index;
    int member = room < 0 ? -1 : pr_room_find_member(&app->rooms[room], c->user_id);
    uint16_t status = PR_WRONG_STATE;
    if (member >= 0 && app->rooms[room].state == PR_IN_GAME &&
        app->rooms[room].members[member].role == PR_PLAYER && app->matches[room])
        status = pr_game_submit(app->matches[room], c->user_id, id, round_no, amount, c->request_started);
    uint8_t accepted = 1;
    if (status == PR_OK && room >= 0) {
        uint8_t ev_buf[17];
        pr_writer w;
        pr_writer_init(&w, ev_buf, sizeof(ev_buf));
        pr_put_u64(&w, c->user_id);
        pr_put_u8(&w, round_no);
        pr_put_u64(&w, amount);
        pr_header ev_h = {PR_EVENT, PR_SUBMIT_GUESS, (uint16_t)w.used, 0};
        for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
            pr_client *sc = &app->clients[i];
            if (sc->fd >= 0 && !sc->drop && sc->room_index == room) {
                int sc_mem = pr_room_find_member(&app->rooms[room], sc->user_id);
                if (sc_mem >= 0 && app->rooms[room].members[sc_mem].role == PR_SPECTATOR) {
                    pr_tx_frame(&sc->tx, &ev_h, ev_buf);
                }
            }
        }
    }
    return pr_reply(c, h, status, &accepted, status == PR_OK ? 1 : 0);
}
void pr_game_tick_all(pr_app *app, double now) {
    for (int i = 0; i < PR_MAX_ROOMS; ++i) {
        pr_match *m = app->matches[i];
        if (!m) continue;
        int flags = pr_game_advance(m, now);
        if (flags & PR_TICK_RESULT) event(app, i, PR_ROUND_RESULT);
        if (flags & PR_TICK_ROUND) event(app, i, PR_NEW_ROUND);
        if (flags & PR_TICK_OVER) finish(app, i);
    }
}
double pr_game_wait(pr_app *app, double now) {
    double wait = 0.1;
    for (size_t i = 0; i < PR_MAX_ROOMS; ++i) {
        pr_match *m = app->matches[i];
        if (m && m->state == PR_RUNNING) {
            double next = (m->phase == PR_ACCEPTING ? m->deadline : m->reveal_until) - now;
            if (next < wait) wait = next;
        }
    }
    return wait > 0 ? wait : 0;
}
int pr_game_detached(pr_app *app, int room, uint64_t user_id) {
    if (app->shutting_down) return 0;
    pr_match *m = app->matches[room];
    if (m && pr_game_disconnect(m, user_id)) finish(app, room);
    return 0;
}
int pr_game_snapshot_send(pr_app *app, pr_client *c) {
    if (c->room_index < 0 || app->rooms[c->room_index].state != PR_IN_GAME) return 0;
    pr_match *m = app->matches[c->room_index];
    if (!m) return -1;
    pr_game_view view;
    pr_game_public(m, pr_server_now(), &view);
    if (m->save_pending) view.state = m->current < 0 ? PR_PREPARING : PR_RUNNING;
    return send_view(c, PR_GAME_SNAPSHOT, &view);
}
void pr_game_cleanup(pr_app *app) {
    for (size_t i = 0; i < PR_MAX_ROOMS; ++i) { free(app->matches[i]); app->matches[i] = NULL; }
}
