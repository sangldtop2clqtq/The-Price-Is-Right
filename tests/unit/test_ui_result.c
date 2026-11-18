#include "pr_ui_state.h"
#include "pr_game_wire.h"
#include "pr_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_round_result_decoding_and_formatting(void) {
    pr_game_view gv;
    memset(&gv, 0, sizeof(gv));
    gv.match_id = 8888;
    gv.round_no = 4;
    gv.count = 3;

    gv.result.match_id = 8888;
    gv.result.round_no = 4;
    gv.result.actual_total = 425000;
    gv.result.count = 3;

    gv.result.scores[0].user_id = 101;
    gv.result.scores[0].submitted = 1;
    gv.result.scores[0].guess = 400000;
    gv.result.scores[0].response_ms = 4500;
    gv.result.scores[0].score = 941;
    gv.result.scores[0].total = 1791;

    gv.result.scores[1].user_id = 102;
    gv.result.scores[1].submitted = 1;
    gv.result.scores[1].guess = 450000;
    gv.result.scores[1].response_ms = 3200;
    gv.result.scores[1].score = 941;
    gv.result.scores[1].total = 1731;

    gv.result.scores[2].user_id = 103;
    gv.result.scores[2].submitted = 0;
    gv.result.scores[2].guess = 0;
    gv.result.scores[2].response_ms = 0;
    gv.result.scores[2].score = 0;
    gv.result.scores[2].total = 1000;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_game_encode(buffer, sizeof(buffer), PR_ROUND_RESULT, &gv);
    assert(enc_len > 0);

    pr_game_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_ok = pr_game_decode(&decoded, PR_ROUND_RESULT, buffer, enc_len);
    assert(dec_ok == 1);
    assert(decoded.match_id == 8888);
    assert(decoded.round_no == 4);
    assert(decoded.result.actual_total == 425000);
    assert(decoded.result.count == 3);

    /* Kiểm tra format VND giá thật */
    char vnd_actual[64];
    pr_format_vnd(decoded.result.actual_total, vnd_actual, sizeof(vnd_actual));
    assert(strcmp(vnd_actual, "425.000 ₫") == 0);

    /* Kiểm tra format VND giá đoán người chơi 1 */
    char vnd_guess1[64];
    pr_format_vnd(decoded.result.scores[0].guess, vnd_guess1, sizeof(vnd_guess1));
    assert(strcmp(vnd_guess1, "400.000 ₫") == 0);
    assert(decoded.result.scores[0].score == 941);
    assert(decoded.result.scores[0].total == 1791);

    /* Người chơi 3 không nộp giá */
    assert(decoded.result.scores[2].submitted == 0);
    assert(decoded.result.scores[2].score == 0);
}

static void test_game_over_finished_and_rankings(void) {
    pr_game_view gv;
    memset(&gv, 0, sizeof(gv));
    gv.match_id = 7777;
    gv.state = PR_FINISHED;
    gv.count = 3;
    gv.abort_reason = PR_ABORT_NONE;

    gv.rankings[0].user_id = 101;
    gv.rankings[0].total = 4250;
    gv.rankings[0].rank = 1;

    gv.rankings[1].user_id = 102;
    gv.rankings[1].total = 3810;
    gv.rankings[1].rank = 2;

    gv.rankings[2].user_id = 103;
    gv.rankings[2].total = 3100;
    gv.rankings[2].rank = 3;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_game_encode(buffer, sizeof(buffer), PR_GAME_OVER, &gv);
    assert(enc_len > 0);

    pr_game_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_ok = pr_game_decode(&decoded, PR_GAME_OVER, buffer, enc_len);
    assert(dec_ok == 1);
    assert(decoded.match_id == 7777);
    assert(decoded.state == PR_FINISHED);
    assert(decoded.count == 3);
    assert(decoded.abort_reason == PR_ABORT_NONE);
    assert(decoded.rankings[0].user_id == 101);
    assert(decoded.rankings[0].rank == 1);
    assert(decoded.rankings[0].total == 4250);
    assert(decoded.rankings[1].rank == 2);
    assert(decoded.rankings[2].rank == 3);
}

static void test_game_over_aborted(void) {
    pr_game_view gv;
    memset(&gv, 0, sizeof(gv));
    gv.match_id = 6666;
    gv.state = PR_ABORTED;
    gv.count = 2;
    gv.abort_reason = PR_ABORT_PLAYERS;

    gv.rankings[0].user_id = 101;
    gv.rankings[0].total = 500;
    gv.rankings[0].rank = 0; /* khi aborted rank phải là 0 */

    gv.rankings[1].user_id = 102;
    gv.rankings[1].total = 400;
    gv.rankings[1].rank = 0;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_game_encode(buffer, sizeof(buffer), PR_GAME_OVER, &gv);
    assert(enc_len > 0);

    pr_game_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_ok = pr_game_decode(&decoded, PR_GAME_OVER, buffer, enc_len);
    assert(dec_ok == 1);
    assert(decoded.match_id == 6666);
    assert(decoded.state == PR_ABORTED);
    assert(decoded.abort_reason == PR_ABORT_PLAYERS);
}

static void test_game_save_failed(void) {
    pr_game_view gv;
    memset(&gv, 0, sizeof(gv));
    gv.match_id = 5555;
    gv.count = 2; /* satisfy validation */
    gv.save_status = PR_INTERNAL_ERROR;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_game_encode(buffer, sizeof(buffer), PR_GAME_SAVE_FAILED, &gv);
    assert(enc_len > 0);

    pr_game_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_ok = pr_game_decode(&decoded, PR_GAME_SAVE_FAILED, buffer, enc_len);
    assert(dec_ok == 1);
    assert(decoded.match_id == 5555);
    assert(decoded.save_status == PR_INTERNAL_ERROR);
}

int main(void) {
    test_round_result_decoding_and_formatting();
    test_game_over_finished_and_rankings();
    test_game_over_aborted();
    test_game_save_failed();

    printf("PASS: UI-06 Round results, game over rankings, abort states and DB save failure\n");
    return 0;
}
