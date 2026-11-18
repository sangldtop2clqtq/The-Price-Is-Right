#include "pr_server.h"
#include <string.h>

static size_t snapshot(pr_room *room, pr_client *client, uint8_t *out, size_t capacity) {
    pr_room_view view;
    return pr_room_snapshot(room, client->user_id, &view) ? pr_room_encode(out, capacity, &view) : 0;
}
void pr_room_broadcast(pr_app *app, int room_index) {
    pr_room *room = &app->rooms[room_index];
    if (!room->code[0] || app->shutting_down) return;
    for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
        pr_client *c = &app->clients[i];
        if (c->fd < 0 || c->drop || c->room_index != room_index) continue;
        uint8_t payload[PR_MAX_PAYLOAD];
        size_t n = snapshot(room, c, payload, sizeof(payload));
        pr_header event = {PR_EVENT, PR_ROOM_UPDATE, (uint16_t)n, 0};
        /* Defer cleanup; never modify membership recursively during fan-out. */
        if (!n || pr_tx_frame(&c->tx, &event, payload) < 0) {
            c->drop = 1; c->close_reason = "tx_queue_limit";
        }
    }
}
void pr_room_detach(pr_client *c) {
    pr_social_detach(c);
    int index = c->room_index;
    c->room_index = -1;
    if (!c->app || index < 0 || index >= PR_MAX_ROOMS) return;
    char code[7];
    strcpy(code, c->app->rooms[index].code);
    pr_room_remove(&c->app->rooms[index], c->user_id);
    pr_log_event("room_leave", c->generation, c->user_id, code,
                 0, PR_LEAVE_ROOM, 0, 0, "membership_removed");
    if (!pr_game_detached(c->app, index, c->user_id)) pr_room_broadcast(c->app, index);
}
static int find_room(pr_app *app, const char *code) {
    for (int i = 0; i < PR_MAX_ROOMS; ++i) if (!strcmp(app->rooms[i].code, code)) return i;
    return -1;
}
static int create_code(pr_app *app, char code[7]) {
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        for (size_t i = 0; i < 6; ++i) code[i] = alphabet[randombytes_uniform(36)];
        code[6] = 0;
        if (find_room(app, code) < 0) return 1;
    }
    return 0;
}
int pr_room_handle(pr_app *app, size_t slot, const pr_header *h, const uint8_t *payload) {
    pr_client *c = &app->clients[slot];
    if (!c->user_id) return pr_reply(c, h, PR_NOT_AUTHENTICATED, NULL, 0);
    if (h->code == PR_SUBMIT_GUESS) return pr_game_guess(app, slot, h, payload);
    char code[7] = {0};
    uint8_t ready = 0;
    if (h->code == PR_JOIN_ROOM) {
        pr_reader r;
        size_t length;
        pr_reader_init(&r, payload, h->payload_length);
        const uint8_t *value = pr_get_string(&r, &length, 6);
        if (!value || length != 6 || !pr_reader_done(&r) || memchr(value, 0, length))
            return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
        memcpy(code, value, 6);
        if (!pr_valid_room_code(code)) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    } else if (h->code == PR_SET_READY) {
        if (h->payload_length != 1 || payload[0] > 1) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
        ready = payload[0];
    } else if (h->payload_length) return pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
    uint16_t status = PR_OK;
    int index = c->room_index;
    if (h->code == PR_CREATE_ROOM || h->code == PR_JOIN_ROOM) {
        if (index >= 0) return pr_reply(c, h, PR_WRONG_STATE, NULL, 0);
        if (h->code == PR_CREATE_ROOM) {
            index = 0;
            while (index < PR_MAX_ROOMS && app->rooms[index].code[0]) ++index;
            if (index == PR_MAX_ROOMS || !create_code(app, code))
                return pr_reply(c, h, PR_INTERNAL_ERROR, NULL, 0);
            strcpy(app->rooms[index].code, code);
        } else {
            index = find_room(app, code);
            if (index < 0) return pr_reply(c, h, PR_ROOM_NOT_FOUND, NULL, 0);
        }
        status = pr_room_add(&app->rooms[index], c->user_id, c->display_name);
        if (status != PR_OK) {
            if (h->code == PR_CREATE_ROOM) memset(&app->rooms[index], 0, sizeof(pr_room));
            return pr_reply(c, h, status, NULL, 0);
        }
        pr_social_detach(c);
        c->room_index = index;
        uint8_t body[PR_MAX_PAYLOAD - 2];
        size_t n = snapshot(&app->rooms[index], c, body, sizeof(body));
        int result = n ? pr_reply(c, h, PR_OK, body, n) : -1;
        if (result == 0 && pr_game_snapshot_send(app, c) < 0) result = -1;
        pr_room_broadcast(app, index);
        return result;
    }
    if (index < 0) return pr_reply(c, h, PR_WRONG_STATE, NULL, 0);
    if (h->code == PR_LEAVE_ROOM) {
        int result = pr_reply(c, h, PR_OK, NULL, 0);
        pr_room_detach(c);
        return result;
    }
    if (h->code == PR_START_GAME) return pr_game_start(app, slot, h);
    status = pr_room_ready(&app->rooms[index], c->user_id, ready);
    int result = pr_reply(c, h, status, NULL, 0);
    if (status == PR_OK) pr_room_broadcast(app, index);
    return result;
}
