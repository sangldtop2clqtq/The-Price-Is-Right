#include "pr_ui_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_initialization_and_defaults(void) {
    pr_ui_state s;
    pr_ui_state_init(&s);

    assert(s.conn_status == PR_CONN_IDLE);
    assert(strcmp(s.server_ip, "127.0.0.1") == 0);
    assert(s.server_port == 6666);
    assert(!s.is_authenticated);
    assert(!s.in_room);
    assert(!s.in_game);
    assert(!s.has_acked_guess);
    assert(!s.has_round_result);
    assert(!s.has_final_result);
    assert(s.current_screen == PR_SCREEN_CONNECT);
}

static void test_reset_functions(void) {
    pr_ui_state s;
    pr_ui_state_init_mock(&s);

    assert(s.in_room);
    assert(s.in_game);
    assert(s.is_authenticated);

    /* Reset game */
    pr_ui_state_reset_game(&s);
    assert(!s.in_game);
    assert(!s.has_acked_guess);
    assert(!s.has_round_result);
    assert(!s.has_final_result);
    assert(s.in_room); /* Room vẫn giữ */

    /* Reset room */
    pr_ui_state_reset_room(&s);
    assert(!s.in_room);
    assert(s.is_authenticated); /* Session vẫn giữ */

    /* Reset session */
    pr_ui_state_init_mock(&s);
    pr_ui_state_reset_session(&s);
    assert(!s.is_authenticated);
    assert(s.self_user_id == 0);
    assert(!s.in_room);
    assert(!s.in_game);
}

static void test_mock_state_consistency(void) {
    pr_ui_state s;
    pr_ui_state_init_mock(&s);

    assert(s.conn_status == PR_CONN_CONNECTED);
    assert(s.is_authenticated);
    assert(strcmp(s.self_username, "lequoctuan") == 0);
    assert(s.self_user_id == 101);

    /* Room */
    assert(s.in_room);
    assert(strcmp(s.room.code, "ABC123") == 0);
    assert(s.room.player_count == 3);
    assert(s.room.spectator_count == 1);
    assert(s.room.players[0].ready == 1);
    assert(s.room.players[2].ready == 0);

    /* Game */
    assert(s.in_game);
    assert(s.game.round_no == 4);
    assert(s.game.type == PR_BASKET);
    assert(s.game.item_count == 2);
    assert(s.has_acked_guess);
    assert(s.acked_guess_amount == 400000);

    /* Result */
    assert(s.has_round_result);
    assert(s.last_round_result.actual_total == 425000);
    assert(s.has_final_result);
    assert(s.save_status == 1);

    /* Stats & History */
    assert(s.stats_loaded);
    assert(s.stats.finished == 12);
    assert(s.history_count == 3);
}

int main(void) {
    test_initialization_and_defaults();
    test_reset_functions();
    test_mock_state_consistency();

    printf("PASS: UI state initialization, resets and mock data consistency\n");
    return 0;
}
