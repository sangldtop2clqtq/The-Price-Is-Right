#include "pr_room.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_roles_fifo_and_host(void) {
    pr_room room;
    memset(&room, 0, sizeof(room)); strcpy(room.code, "ABC123");
    for (uint64_t id = 1; id <= 4; ++id) assert(pr_room_add(&room, id, "player") == PR_OK);
    assert(pr_room_can_start(&room, 1) == PR_WRONG_STATE);
    for (uint64_t id = 1; id <= 4; ++id) assert(pr_room_ready(&room, id, 1) == PR_OK);
    assert(pr_room_can_start(&room, 1) == PR_OK);
    assert(pr_room_can_start(&room, 2) == PR_NOT_ROOM_OWNER);
    assert(pr_room_add(&room, 5, "spectator5") == PR_OK);
    assert(pr_room_add(&room, 6, "spectator6") == PR_OK);
    assert(pr_room_can_start(&room, 1) == PR_OK); /* watchers do not reset ready */
    assert(pr_room_ready(&room, 5, 1) == PR_WRONG_STATE);
    assert(pr_room_can_start(&room, 5) == PR_WRONG_STATE);
    assert(pr_room_remove(&room, 5) == PR_OK);
    assert(pr_room_can_start(&room, 1) == PR_OK);
    assert(pr_room_add(&room, 7, "new_spectator") == PR_OK); /* reused array slot */
    assert(pr_room_remove(&room, 1) == PR_OK);
    assert(room.host_id == 2);
    assert(room.members[pr_room_find_member(&room, 6)].role == PR_PLAYER);
    assert(room.members[pr_room_find_member(&room, 7)].role == PR_SPECTATOR);
    for (size_t i = 0; i < PR_MAX_ROOM_MEMBERS; ++i) assert(room.members[i].ready == 0);
    pr_room_view view;
    assert(pr_room_snapshot(&room, 7, &view));
    assert(view.self_role == PR_SPECTATOR && view.player_count == 4 && view.spectator_count == 1);
    uint8_t wire[512];
    size_t n = pr_room_encode(wire, sizeof(wire), &view);
    assert(n > 0);
    pr_room_view decoded;
    assert(pr_room_decode(&decoded, wire, n));
    assert(decoded.players[0].user_id == 2 && decoded.players[3].user_id == 6);
    assert(decoded.self_role == PR_SPECTATOR && !strcmp(decoded.code, "ABC123"));
    assert(!pr_room_decode(&decoded, wire, n - 1));
    wire[n] = 0;
    assert(!pr_room_decode(&decoded, wire, n + 1));
    view.player_count = 5;
    assert(!pr_room_encode(wire, sizeof(wire), &view));
    for (uint64_t id = 2; id <= 7; ++id)
        if (pr_room_find_member(&room, id) >= 0) assert(pr_room_remove(&room, id) == PR_OK);
    assert(!room.code[0] && !room.host_id);
}
static void test_running_and_limits(void) {
    pr_room room;
    memset(&room, 0, sizeof(room)); strcpy(room.code, "XYZ789");
    assert(pr_room_add(&room, 1, "host") == PR_OK);
    assert(pr_room_ready(&room, 1, 2) == PR_INVALID_MESSAGE);
    assert(pr_room_can_start(&room, 1) == PR_WRONG_STATE);
    assert(pr_room_add(&room, 1, "host") == PR_WRONG_STATE);
    room.state = PR_IN_GAME; /* Domain-only check; no wire path starts a match yet. */
    for (uint64_t id = 2; id <= PR_MAX_ROOM_MEMBERS; ++id) assert(pr_room_add(&room, id, "watcher") == PR_OK);
    assert(pr_room_add(&room, 65, "overflow") == PR_ROOM_FULL);
    assert(pr_room_ready(&room, 1, 1) == PR_WRONG_STATE);
    assert(pr_room_remove(&room, 1) == PR_OK);
    assert(room.members[pr_room_find_member(&room, 2)].role == PR_SPECTATOR);
    assert(room.host_id == 1); /* no host change or promotion mid-match */
    pr_room_view view;
    assert(pr_room_snapshot(&room, 2, &view) && view.player_count == 0);
    assert(!pr_valid_room_code("abc123") && !pr_valid_room_code("ABC12"));
    assert(pr_valid_room_code("A0B1C2"));
}
int main(void) {
    test_roles_fifo_and_host(); test_running_and_limits();
    puts("PASS: roles, readiness, FIFO, host transfer, snapshots and in-game domain guards");
    return 0;
}
