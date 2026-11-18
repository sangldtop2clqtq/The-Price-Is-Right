#include "pr_game.h"
#include <math.h>
#include <string.h>

void pr_game_init(pr_match *m, uint64_t id, const pr_room_view *room,
                   uint32_t single, uint32_t basket, uint32_t reveal) {
    memset(m, 0, sizeof(*m));
    m->id = id; m->count = room->player_count; m->current = -1;
    m->single_ms = single; m->basket_ms = basket; m->reveal_ms = reveal;
    for (size_t i = 0; i < m->count; ++i) {
        m->participants[i].user_id = room->players[i].user_id;
        strcpy(m->participants[i].name, room->players[i].name);
        m->participants[i].connected = 1;
    }
}
static void begin_round(pr_match *m, double now) {
    ++m->current;
    m->phase = PR_ACCEPTING; m->started = now;
    m->deadline = now + (double)m->rounds[m->current].public.duration_ms / 1000.0;
}
int pr_game_prepare(pr_match *m, const pr_product *products, size_t count, uint32_t seed, double now) {
    if (m->state != PR_PREPARING || m->count < 2 || m->count > 4 || count < 3 || count > PR_MAX_PRODUCTS ||
        !m->single_ms || !m->basket_ms || !m->reveal_ms) return 0;
    for (size_t r = 0; r < PR_ROUNDS; ++r) {
        pr_game_round *round = &m->rounds[r];
        pr_game_view *v = &round->public;
        v->match_id = m->id; v->round_no = (uint8_t)(r + 1);
        v->type = r < 3 ? PR_SINGLE : PR_BASKET;
        v->item_count = r < 3 ? 1 : (uint16_t)(r - 1);
        v->duration_ms = r < 3 ? m->single_ms : m->basket_ms;
        for (size_t i = 0; i < v->item_count; ++i) {
            const pr_product *p = &products[((size_t)seed + r * 3 + i) % count];
            if (!p->price || p->price > PR_MAX_PRICE) return 0;
            v->items[i] = p->item;
            round->unit_prices[i] = p->price;
            v->items[i].quantity = r < 3 ? 1 : (uint16_t)(1 + ((seed / 7 + (uint32_t)r + (uint32_t)i) % 3));
            round->actual_total += p->price * v->items[i].quantity;
        }
        if (!round->actual_total || round->actual_total > PR_MAX_PRICE) return 0;
        for (size_t i = 0; i < m->count; ++i) round->guesses[i].user_id = m->participants[i].user_id;
    }
    m->state = PR_RUNNING;
    begin_round(m, now);
    return 1;
}
uint16_t pr_game_submit(pr_match *m, uint64_t user_id, uint64_t match_id, uint8_t round_no,
                         uint64_t amount, double received) {
    if (amount > PR_MAX_PRICE) return PR_INVALID_MESSAGE;
    if (m->id != match_id || m->state != PR_RUNNING || m->current < 0 || round_no != m->current + 1) return PR_WRONG_STATE;
    size_t player = 0;
    while (player < m->count && m->participants[player].user_id != user_id) ++player;
    if (player == m->count || !m->participants[player].connected) return PR_WRONG_STATE;
    if (m->phase != PR_ACCEPTING || received >= m->deadline) return PR_ROUND_EXPIRED;
    if (received < m->started) return PR_WRONG_STATE;
    pr_game_round *round = &m->rounds[m->current];
    pr_score_view *guess = &round->guesses[player];
    /* The latest valid submission replaces both the amount and response time. */
    guess->submitted = 1; guess->guess = amount;
    double ms = (received - m->started) * 1000.0;
    guess->response_ms = (uint32_t)ms;
    if (guess->response_ms >= round->public.duration_ms) guess->response_ms = round->public.duration_ms - 1;
    return PR_OK;
}
int pr_game_advance(pr_match *m, double now) {
    if (m->state != PR_RUNNING) return 0;
    if (m->phase == PR_ACCEPTING && now >= m->deadline) {
        pr_game_round *round = &m->rounds[m->current];
        for (size_t i = 0; i < m->count; ++i) {
            pr_score_view *s = &round->guesses[i];
            if (s->submitted) pr_score(s->guess, round->actual_total, s->response_ms, round->public.duration_ms, &s->score);
            m->totals[i] = (uint16_t)(m->totals[i] + s->score); s->total = m->totals[i];
        }
        m->phase = PR_REVEALED; m->reveal_until = now + (double)m->reveal_ms / 1000.0;
        if (m->current == PR_ROUNDS - 1) { m->state = PR_FINISHED; return PR_TICK_RESULT | PR_TICK_OVER; }
        return PR_TICK_RESULT;
    }
    if (m->phase == PR_REVEALED && now >= m->reveal_until) { begin_round(m, now); return PR_TICK_ROUND; }
    return 0;
}
void pr_game_abort(pr_match *m, uint8_t reason) {
    if (m->state != PR_PREPARING && m->state != PR_RUNNING) return;
    m->state = PR_ABORTED; m->abort_reason = reason;
}
int pr_game_disconnect(pr_match *m, uint64_t user_id) {
    if (m->state != PR_PREPARING && m->state != PR_RUNNING) return 0;
    unsigned connected = 0;
    for (size_t i = 0; i < m->count; ++i) {
        if (m->participants[i].user_id == user_id) m->participants[i].connected = 0;
        connected += m->participants[i].connected != 0;
    }
    if (connected < 2) { pr_game_abort(m, PR_ABORT_PLAYERS); return 1; }
    return 0;
}
void pr_game_public(const pr_match *m, double now, pr_game_view *v) {
    memset(v, 0, sizeof(*v));
    if (m->current >= 0) {
        *v = m->rounds[m->current].public;
        v->round_state = m->phase;
        if (m->phase == PR_ACCEPTING) {
            double remaining = ceil((m->deadline - now) * 1000.0);
            v->remaining_ms = remaining <= 0 ? 0 : remaining > v->duration_ms ? v->duration_ms : (uint32_t)remaining;
        }
        if (m->phase == PR_REVEALED) {
            v->has_result = 1; v->result.match_id = m->id; v->result.round_no = v->round_no;
            v->result.actual_total = m->rounds[m->current].actual_total; v->result.count = m->count;
            memcpy(v->result.scores, m->rounds[m->current].guesses, sizeof(v->result.scores));
        }
    }
    v->match_id = m->id; v->state = m->state; v->abort_reason = m->abort_reason;
    v->count = m->count;
    memcpy(v->participants, m->participants, sizeof(v->participants));
    memcpy(v->totals, m->totals, sizeof(v->totals));
    for (size_t i = 0; i < m->count; ++i) {
        pr_ranking *p = &v->rankings[i];
        p->user_id = m->participants[i].user_id; p->total = m->totals[i];
        if (m->state == PR_FINISHED) {
            p->rank = 1;
            for (size_t j = 0; j < m->count; ++j) if (m->totals[j] > m->totals[i]) ++p->rank;
        }
    }
}
