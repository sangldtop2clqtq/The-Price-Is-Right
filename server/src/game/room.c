#include "pr_room.h"
#include <string.h>

int pr_room_find_member(const pr_room *r, uint64_t user_id) {
    if (!user_id) return -1;
    for (int i = 0; i < PR_MAX_ROOM_MEMBERS; ++i) if (r->members[i].user_id == user_id) return i;
    return -1;
}
static unsigned player_count(const pr_room *r) {
    unsigned count = 0;
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i)
        if (r->members[i].user_id && r->members[i].role == PR_PLAYER) ++count;
    return count;
}
static void reset_ready(pr_room *r) {
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i) r->members[i].ready = 0;
}
static int earliest(const pr_room *r, uint8_t role, uint64_t after) {
    int best = -1;
    for (int i = 0; i < PR_MAX_ROOM_MEMBERS; ++i) {
        const pr_member *m = &r->members[i];
        if (m->user_id && m->role == role && m->joined_order > after &&
            (best < 0 || m->joined_order < r->members[best].joined_order)) best = i;
    }
    return best;
}
void pr_room_finish(pr_room *r) {
    if (!r->code[0]) return;
    r->state = PR_WAITING;
    while (player_count(r) < PR_MAX_PLAYERS) {
        int next = earliest(r, PR_SPECTATOR, 0);
        if (next < 0) break;
        r->members[next].role = PR_PLAYER;
    }
    if (pr_room_find_member(r, r->host_id) < 0) {
        int next = earliest(r, PR_PLAYER, 0);
        r->host_id = next < 0 ? 0 : r->members[next].user_id;
    }
    reset_ready(r);
    if (r->revision < UINT32_MAX) ++r->revision;
}
uint16_t pr_room_add(pr_room *r, uint64_t user_id, const char *name) {
    if (!r->code[0] || !user_id || !*name || strlen(name) > 96) return PR_INVALID_MESSAGE;
    if (pr_room_find_member(r, user_id) >= 0) return PR_WRONG_STATE;
    if (r->revision == UINT32_MAX || r->next_order == UINT64_MAX) return PR_INTERNAL_ERROR;
    int slot = 0;
    while (slot < PR_MAX_ROOM_MEMBERS && r->members[slot].user_id) ++slot;
    if (slot == PR_MAX_ROOM_MEMBERS) return PR_ROOM_FULL;
    pr_member *m = &r->members[slot];
    m->role = r->state == PR_WAITING && player_count(r) < PR_MAX_PLAYERS ? PR_PLAYER : PR_SPECTATOR;
    if (m->role == PR_PLAYER) reset_ready(r);
    m->user_id = user_id; m->joined_order = ++r->next_order; m->ready = 0;
    strcpy(m->name, name);
    if (!r->host_id) r->host_id = user_id;
    ++r->revision;
    return PR_OK;
}
uint16_t pr_room_remove(pr_room *r, uint64_t user_id) {
    int slot = pr_room_find_member(r, user_id);
    if (slot < 0) return PR_WRONG_STATE;
    uint8_t removed_role = r->members[slot].role;
    memset(&r->members[slot], 0, sizeof(r->members[slot]));
    int any = 0;
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i) any |= r->members[i].user_id != 0;
    if (!any) { memset(r, 0, sizeof(*r)); return PR_OK; }
    if (r->state == PR_WAITING) {
        if (removed_role == PR_PLAYER) {
            while (player_count(r) < PR_MAX_PLAYERS) {
                int next = earliest(r, PR_SPECTATOR, 0);
                if (next < 0) break;
                r->members[next].role = PR_PLAYER;
            }
            reset_ready(r);
        }
        if (r->host_id == user_id) {
            int host = earliest(r, PR_PLAYER, 0);
            r->host_id = host >= 0 ? r->members[host].user_id : 0;
        }
    }
    /* Leave/cleanup must remain possible even at revision saturation. */
    if (r->revision != UINT32_MAX) ++r->revision;
    return PR_OK;
}
uint16_t pr_room_ready(pr_room *r, uint64_t user_id, uint8_t ready) {
    if (ready > 1) return PR_INVALID_MESSAGE;
    int slot = pr_room_find_member(r, user_id);
    if (slot < 0 || r->state != PR_WAITING || r->members[slot].role != PR_PLAYER) return PR_WRONG_STATE;
    if (r->members[slot].ready == ready) return PR_OK;
    if (r->revision == UINT32_MAX) return PR_INTERNAL_ERROR;
    r->members[slot].ready = ready; ++r->revision;
    return PR_OK;
}
uint16_t pr_room_can_start(const pr_room *r, uint64_t user_id) {
    int slot = pr_room_find_member(r, user_id);
    if (slot < 0 || r->state != PR_WAITING || r->members[slot].role != PR_PLAYER) return PR_WRONG_STATE;
    if (r->host_id != user_id) return PR_NOT_ROOM_OWNER;
    if (player_count(r) < 2) return PR_WRONG_STATE;
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i)
        if (r->members[i].user_id && r->members[i].role == PR_PLAYER && !r->members[i].ready) return PR_WRONG_STATE;
    return PR_OK;
}
int pr_room_snapshot(const pr_room *r, uint64_t user_id, pr_room_view *view) {
    int self = pr_room_find_member(r, user_id);
    if (self < 0) return 0;
    memset(view, 0, sizeof(*view));
    strcpy(view->code, r->code);
    view->state = r->state; view->host_id = r->host_id; view->revision = r->revision;
    view->self_role = r->members[self].role;
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i)
        if (r->members[i].user_id && r->members[i].role == PR_SPECTATOR) ++view->spectator_count;
    uint64_t after = 0;
    for (size_t i = 0; i < PR_MAX_PLAYERS; ++i) {
        int next = earliest(r, PR_PLAYER, after);
        if (next < 0) break;
        const pr_member *m = &r->members[next];
        pr_player_view *p = &view->players[view->player_count++];
        p->user_id = m->user_id; strcpy(p->name, m->name); p->ready = m->ready; p->connected = 1;
        after = m->joined_order;
    }
    return 1;
}
