#ifndef PR_GAME_WIRE_H
#define PR_GAME_WIRE_H
#include "pr_room_wire.h"
#define PR_ROUNDS 5
#define PR_MAX_ITEMS 3
enum pr_match_state { PR_PREPARING = 0, PR_RUNNING = 1, PR_FINISHED = 2, PR_ABORTED = 3 };
enum pr_round_state { PR_ROUND_PREPARING = 0, PR_ACCEPTING = 1, PR_REVEALED = 2 };
enum pr_round_type { PR_SINGLE = 0, PR_BASKET = 1 };
enum pr_abort_reason { PR_ABORT_NONE = 0, PR_ABORT_PLAYERS = 1, PR_ABORT_SERVER = 2 };
typedef struct { uint64_t user_id; char name[97]; uint8_t connected; } pr_participant;
typedef struct { uint64_t product_id; char name[97], image_key[65]; uint16_t quantity; } pr_item;
typedef struct {
    uint64_t user_id, guess;
    uint32_t response_ms;
    uint16_t score, total;
    uint8_t submitted;
} pr_score_view;
typedef struct {
    uint64_t match_id, actual_total;
    uint8_t round_no;
    uint16_t count;
    pr_score_view scores[PR_MAX_PLAYERS];
} pr_result_view;
typedef struct { uint64_t user_id; uint16_t total; uint8_t rank; } pr_ranking;
/* Superset for all public events. No secret price/guess exists in NEW_ROUND fields. */
typedef struct {
    uint64_t match_id;
    uint8_t state, round_no, round_state, type, abort_reason, has_result;
    uint32_t duration_ms, remaining_ms;
    uint16_t count, item_count;
    uint16_t save_status;
    pr_participant participants[PR_MAX_PLAYERS];
    pr_item items[PR_MAX_ITEMS];
    uint16_t totals[PR_MAX_PLAYERS];
    pr_result_view result;
    pr_ranking rankings[PR_MAX_PLAYERS];
} pr_game_view;
size_t pr_game_encode(uint8_t *, size_t, uint16_t code, const pr_game_view *);
int pr_game_decode(pr_game_view *, uint16_t code, const uint8_t *, size_t);
#endif
