#include "pr_game.h"
#include "pr_room.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void setup(pr_match *m, unsigned players, uint32_t seed) {
    pr_room_view room;
    memset(&room, 0, sizeof(room)); room.player_count = (uint16_t)players;
    for (unsigned i = 0; i < players; ++i) {
        room.players[i].user_id = i + 1;
        strcpy(room.players[i].name, "player");
    }
    pr_game_init(m, 42, &room, 20000, 30000, 5000);
    pr_product products[5];
    memset(products, 0, sizeof(products));
    for (size_t i = 0; i < 5; ++i) {
        products[i].item.product_id = i + 1; products[i].price = (i + 1) * 100000;
        strcpy(products[i].item.name, "product"); strcpy(products[i].item.image_key, "products/item.png");
    }
    assert(pr_game_prepare(m, products, 5, seed, 100));
}
static void wire_roundtrip(uint16_t code, const pr_game_view *v) {
    uint8_t payload[4096];
    size_t n = pr_game_encode(payload, sizeof(payload), code, v);
    assert(n > 0 && n < 4096);
    pr_game_view decoded;
    assert(pr_game_decode(&decoded, code, payload, n));
    assert(decoded.match_id == v->match_id);
    assert(!pr_game_decode(&decoded, code, payload, n - 1));
    payload[n] = 0;
    assert(!pr_game_decode(&decoded, code, payload, n + 1));
}
static void test_five_rounds_and_ties(void) {
    pr_match m;
    setup(&m, 3, 0);
    double now = 100;
    for (int round = 0; round < 5; ++round) {
        assert(m.current == round && m.phase == PR_ACCEPTING && m.state == PR_RUNNING);
        pr_game_round *r = &m.rounds[round];
        assert(r->public.duration_ms == (round < 3 ? 20000 : 30000));
        assert(r->public.item_count == (round < 3 ? 1 : round == 3 ? 2 : 3));
        assert(pr_game_submit(&m, 1, 42, (uint8_t)(round + 1), r->actual_total, now) == PR_OK);
        assert(pr_game_submit(&m, 2, 42, (uint8_t)(round + 1), r->actual_total, now) == PR_OK);
        assert(pr_game_submit(&m, 1, 42, (uint8_t)(round + 1), r->actual_total, now) == PR_OK);
        assert(pr_game_submit(&m, 99, 42, (uint8_t)(round + 1), 0, now) == PR_WRONG_STATE);
        pr_game_view v;
        pr_game_public(&m, now, &v);
        assert(!v.has_result && v.totals[0] == round * 1000);
        wire_roundtrip(PR_NEW_ROUND, &v); wire_roundtrip(PR_GAME_SNAPSHOT, &v);
        assert(pr_game_advance(&m, m.deadline - 0.001) == 0); /* all submitted still wait */
        now = m.deadline;
        int flags = pr_game_advance(&m, now);
        assert(flags & PR_TICK_RESULT);
        pr_game_public(&m, now, &v);
        assert(v.has_result && v.result.actual_total == r->actual_total);
        assert(v.result.scores[2].submitted == 0 && v.result.scores[2].score == 0);
        wire_roundtrip(PR_ROUND_RESULT, &v); wire_roundtrip(PR_GAME_SNAPSHOT, &v);
        if (round == 4) {
            assert(flags & PR_TICK_OVER);
            assert(v.rankings[0].rank == 1 && v.rankings[1].rank == 1 && v.rankings[2].rank == 3);
            assert(v.rankings[0].total == 5000);
            wire_roundtrip(PR_GAME_OVER, &v);
        } else {
            assert(pr_game_submit(&m, 3, 42, (uint8_t)(round + 1), 0, now - 0.001) == PR_ROUND_EXPIRED);
            assert(pr_game_advance(&m, m.reveal_until - 0.001) == 0);
            now = m.reveal_until;
            assert(pr_game_advance(&m, now) == PR_TICK_ROUND);
        }
    }
    assert(m.state == PR_FINISHED && pr_game_advance(&m, now + 999) == 0);
}
static void test_deadlines_disconnect_and_privacy(void) {
    pr_match m;
    setup(&m, 4, 0);
    assert(pr_game_submit(&m, 1, 43, 1, 0, 100) == PR_WRONG_STATE);
    assert(pr_game_submit(&m, 1, 42, 2, 0, 100) == PR_WRONG_STATE);
    assert(pr_game_submit(&m, 1, 42, 1, 0, 99) == PR_WRONG_STATE);
    assert(pr_game_submit(&m, 1, 42, 1, 0, 120) == PR_ROUND_EXPIRED);
    assert(pr_game_submit(&m, 1, 42, 1, PR_MAX_PRICE + 1, 100) == PR_INVALID_MESSAGE);
    assert(pr_game_submit(&m, 1, 42, 1, 100000, 100) == PR_OK);
    assert(!pr_game_disconnect(&m, 1)); /* preserve submitted guess */
    assert(!pr_game_disconnect(&m, 99)); /* spectator irrelevant */
    assert(!pr_game_disconnect(&m, 2));
    assert(m.state == PR_RUNNING && m.rounds[0].guesses[0].submitted);
    pr_game_view v;
    pr_game_public(&m, 101, &v);
    assert(!v.has_result && !v.participants[0].connected && !v.totals[0]);
    uint8_t payload[4096];
    size_t n = pr_game_encode(payload, sizeof(payload), PR_GAME_SNAPSHOT, &v);
    pr_game_view decoded;
    assert(pr_game_decode(&decoded, PR_GAME_SNAPSHOT, payload, n));
    assert(!decoded.has_result && !decoded.result.actual_total && !decoded.result.scores[0].guess);
    assert(pr_game_disconnect(&m, 3));
    pr_game_public(&m, 102, &v);
    assert(m.state == PR_ABORTED && v.abort_reason == PR_ABORT_PLAYERS && v.rankings[0].rank == 0);
    wire_roundtrip(PR_GAME_OVER, &v);
    assert(pr_game_advance(&m, 200) == 0);
}
static void test_latest_guess_and_time_replace_previous(void) {
    pr_match m;
    setup(&m, 2, 0);
    assert(pr_game_submit(&m, 1, 42, 1, 3000, 101) == PR_OK);
    assert(pr_game_submit(&m, 1, 42, 1, 4000, 105) == PR_OK);
    pr_score_view *guess = &m.rounds[0].guesses[0];
    assert(guess->guess == 4000 && guess->response_ms == 5000);
    assert(pr_game_submit(&m, 1, 42, 1, PR_MAX_PRICE + 1, 106) == PR_INVALID_MESSAGE);
    assert(pr_game_submit(&m, 1, 42, 2, 8000, 106) == PR_WRONG_STATE);
    assert(pr_game_submit(&m, 1, 42, 1, 9000, m.deadline) == PR_ROUND_EXPIRED);
    assert(guess->guess == 4000 && guess->response_ms == 5000);
    assert(pr_game_advance(&m, m.deadline) & PR_TICK_RESULT);
    uint16_t expected;
    assert(pr_score(4000, m.rounds[0].actual_total, 5000, 20000, &expected));
    assert(guess->score == expected);
    assert(pr_game_submit(&m, 1, 42, 1, 5000, 119) == PR_ROUND_EXPIRED);
    assert(guess->guess == 4000 && guess->response_ms == 5000);
}
int main(void) {
    test_five_rounds_and_ties(); test_deadlines_disconnect_and_privacy();
    test_latest_guess_and_time_replace_previous();
    puts("PASS: five rounds, latest-guess updates/deadlines, scoring/ties, disconnect and public wire privacy");
    return 0;
}
