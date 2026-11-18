#ifndef PR_GAME_H
#define PR_GAME_H
#include "pr_game_wire.h"
#include "pr_scoring.h"
#define PR_MAX_PRODUCTS 300
enum pr_game_tick { PR_TICK_RESULT = 1, PR_TICK_ROUND = 2, PR_TICK_OVER = 4 };
typedef struct { pr_item item; uint64_t price; } pr_product;
typedef struct {
    pr_game_view public;
    uint64_t actual_total, unit_prices[PR_MAX_ITEMS];
    pr_score_view guesses[4];
} pr_game_round;
typedef struct {
    uint64_t id;
    uint8_t state, phase, abort_reason;
    int current;
    uint16_t count;
    pr_participant participants[4];
    uint16_t totals[4];
    uint32_t single_ms, basket_ms, reveal_ms;
    double started, deadline, reveal_until;
    pr_game_round rounds[PR_ROUNDS];
    char room_code[7];
    uint64_t wall_started_ms, wall_ended_ms;
    int save_pending, persisted, save_failed;
} pr_match;
void pr_game_init(pr_match *, uint64_t, const pr_room_view *, uint32_t, uint32_t, uint32_t);
int pr_game_prepare(pr_match *, const pr_product *, size_t, uint32_t seed, double now);
uint16_t pr_game_submit(pr_match *, uint64_t user_id, uint64_t match_id, uint8_t round,
                        uint64_t amount, double received_at);
int pr_game_advance(pr_match *, double now);
int pr_game_disconnect(pr_match *, uint64_t user_id);
void pr_game_abort(pr_match *, uint8_t reason);
void pr_game_public(const pr_match *, double now, pr_game_view *);
#endif
