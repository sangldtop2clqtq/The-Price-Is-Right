#ifndef PR_ACCOUNT_WIRE_H
#define PR_ACCOUNT_WIRE_H
#include "pr_protocol.h"
#define PR_PAGE_LIMIT 20
typedef struct { uint64_t user_id; char name[97]; } pr_online_view;
typedef struct {
    uint64_t match_id, started_at, ended_at;
    uint8_t state, rank;
    uint16_t total;
} pr_history_view;
typedef struct {
    uint16_t count;
    uint64_t next_cursor;
    pr_online_view players[PR_PAGE_LIMIT];
    pr_history_view history[PR_PAGE_LIMIT];
} pr_page_view;
typedef struct { uint32_t finished, wins, average_x100; uint16_t best; } pr_stats_view;
typedef struct { uint64_t inviter; char code[7]; uint32_t expires_ms; } pr_invite_view;
size_t pr_page_encode(uint8_t *, size_t, uint16_t code, const pr_page_view *);
int pr_page_decode(pr_page_view *, uint16_t code, const uint8_t *, size_t);
size_t pr_stats_encode(uint8_t *, size_t, const pr_stats_view *);
int pr_stats_decode(pr_stats_view *, const uint8_t *, size_t);
size_t pr_invite_encode(uint8_t *, size_t, const pr_invite_view *);
int pr_invite_decode(pr_invite_view *, const uint8_t *, size_t);
#endif
