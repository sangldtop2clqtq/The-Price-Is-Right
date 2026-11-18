#ifndef PR_ROOM_WIRE_H
#define PR_ROOM_WIRE_H
#include "pr_protocol.h"
#define PR_MAX_PLAYERS 4
enum pr_room_state { PR_WAITING = 0, PR_IN_GAME = 1 };
typedef struct {
    uint64_t user_id;
    char name[97];
    uint8_t ready, connected;
} pr_player_view;
typedef struct {
    char code[7];
    uint8_t state, self_role;
    uint64_t host_id;
    uint32_t revision, spectator_count;
    uint16_t player_count;
    pr_player_view players[PR_MAX_PLAYERS];
} pr_room_view;
int pr_valid_room_code(const char *);
size_t pr_room_encode(uint8_t *, size_t, const pr_room_view *);
int pr_room_decode(pr_room_view *, const uint8_t *, size_t);
#endif
