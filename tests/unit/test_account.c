#include "pr_account_wire.h"
#include "pr_persist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    uint8_t bytes[4096];
    pr_page_view page, decoded;
    memset(&page, 0, sizeof(page)); page.count = 20; page.next_cursor = 123;
    for (size_t i = 0; i < 20; ++i) {
        page.players[i].user_id = i + 1;
        memset(page.players[i].name, 'a', 96); page.players[i].name[96] = 0;
    }
    size_t n = pr_page_encode(bytes, sizeof(bytes), PR_LIST_PLAYERS, &page);
    assert(n && n + 2 <= PR_MAX_PAYLOAD && pr_page_decode(&decoded, PR_LIST_PLAYERS, bytes, n));
    assert(decoded.count == 20 && decoded.next_cursor == 123);
    assert(!pr_page_decode(&decoded, PR_LIST_PLAYERS, bytes, n - 1));
    bytes[n] = 0; assert(!pr_page_decode(&decoded, PR_LIST_PLAYERS, bytes, n + 1));
    page.count = 21; assert(!pr_page_encode(bytes, sizeof(bytes), PR_LIST_PLAYERS, &page));
    page.count = 1; page.history[0] = (pr_history_view){100, 1000, 2000, 2, 1, 5000};
    n = pr_page_encode(bytes, sizeof(bytes), PR_GET_HISTORY, &page);
    assert(pr_page_decode(&decoded, PR_GET_HISTORY, bytes, n) && decoded.history[0].total == 5000);
    pr_stats_view stats = {10, 3, 123456, 4999}, stats_copy;
    n = pr_stats_encode(bytes, sizeof(bytes), &stats);
    assert(n == 14 && pr_stats_decode(&stats_copy, bytes, n) && stats_copy.wins == 3);
    stats.wins = 11; n = pr_stats_encode(bytes, sizeof(bytes), &stats);
    assert(!pr_stats_decode(&stats_copy, bytes, n));
    pr_invite_view invite = {42, "ABC123", 60000}, invite_copy;
    n = pr_invite_encode(bytes, sizeof(bytes), &invite);
    assert(pr_invite_decode(&invite_copy, bytes, n) && !strcmp(invite_copy.code, "ABC123"));
    invite.expires_ms = 60001; assert(!pr_invite_encode(bytes, sizeof(bytes), &invite));
    pr_match m;
    memset(&m, 0, sizeof(m)); m.id = 123; m.current = -1; m.state = PR_ABORTED; m.abort_reason = PR_ABORT_SERVER;
    m.count = 2; m.wall_started_ms = 1000; m.wall_ended_ms = 2000; strcpy(m.room_code, "ABC123");
    for (size_t i = 0; i < 2; ++i) {
        m.participants[i].user_id = i + 1; strcpy(m.participants[i].name, "quote\" slash\\ newline\n");
    }
    char *json = pr_match_json(&m);
    assert(json && strstr(json, "quote\\\" slash\\\\ newline\\u000a") && strstr(json, "\"rounds\":[]"));
    assert(!strchr(json, '\n')); free(json);
    puts("PASS: bounded account pages, stats/invite validation and escaped immutable match document");
    return 0;
}
