#ifndef PR_ROOM_H
#define PR_ROOM_H
#include "pr_room_wire.h"
#define PR_MAX_ROOM_MEMBERS 64
#define PR_MAX_ROOMS 64
typedef struct {
    uint64_t user_id, joined_order;
    char name[97];
    uint8_t role, ready;
} pr_member;
typedef struct {
    char code[7];
    uint8_t state;
    uint64_t host_id, next_order;
    uint32_t revision;
    pr_member members[PR_MAX_ROOM_MEMBERS];
} pr_room;
int pr_room_find_member(const pr_room *, uint64_t);
uint16_t pr_room_add(pr_room *, uint64_t, const char *);
uint16_t pr_room_remove(pr_room *, uint64_t);
uint16_t pr_room_ready(pr_room *, uint64_t, uint8_t);
uint16_t pr_room_can_start(const pr_room *, uint64_t);
int pr_room_snapshot(const pr_room *, uint64_t, pr_room_view *);
void pr_room_finish(pr_room *);
#endif
