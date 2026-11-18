#include "pr_ui_state.h"
#include "pr_account_wire.h"
#include "pr_client.h"
#include "pr_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_stats_decoding_and_calculations(void) {
    pr_stats_view sv;
    memset(&sv, 0, sizeof(sv));
    sv.finished = 12;
    sv.wins = 5;
    sv.average_x100 = 365000;
    sv.best = 4720;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_stats_encode(buffer, sizeof(buffer), &sv);
    assert(enc_len > 0);

    pr_stats_view dec;
    memset(&dec, 0, sizeof(dec));
    int dec_ok = pr_stats_decode(&dec, buffer, enc_len);
    assert(dec_ok == 1);
    assert(dec.finished == 12);
    assert(dec.wins == 5);
    assert(dec.average_x100 == 365000);
    assert(dec.best == 4720);

    /* Kiểm tra tính tỷ lệ thắng và điểm trung bình */
    double win_rate = dec.finished ? (100.0 * (double)dec.wins / (double)dec.finished) : 0.0;
    assert(win_rate > 41.6 && win_rate < 41.7); /* ~41.67% */

    double avg_score = (double)dec.average_x100 / 100.0;
    assert(avg_score == 3650.0);
}

static void test_history_paging_decoding(void) {
    pr_page_view pv;
    memset(&pv, 0, sizeof(pv));
    pv.count = 2;
    pv.next_cursor = 1020;

    pv.history[0].match_id = 1042;
    pv.history[0].state = PR_FINISHED;
    pv.history[0].rank = 1;
    pv.history[0].total = 4250;
    pv.history[0].started_at = 1700000000;
    pv.history[0].ended_at = 1700000300;

    pv.history[1].match_id = 1021;
    pv.history[1].state = PR_ABORTED;
    pv.history[1].rank = 0;
    pv.history[1].total = 1200;
    pv.history[1].started_at = 1699800000;
    pv.history[1].ended_at = 1699800100;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_page_encode(buffer, sizeof(buffer), PR_GET_HISTORY, &pv);
    assert(enc_len > 0);

    pr_page_view dec;
    memset(&dec, 0, sizeof(dec));
    int dec_ok = pr_page_decode(&dec, PR_GET_HISTORY, buffer, enc_len);
    assert(dec_ok == 1);
    assert(dec.count == 2);
    assert(dec.next_cursor == 1020);
    assert(dec.history[0].match_id == 1042);
    assert(dec.history[0].state == PR_FINISHED);
    assert(dec.history[0].rank == 1);
    assert(dec.history[0].total == 4250);
    assert(dec.history[1].match_id == 1021);
    assert(dec.history[1].state == PR_ABORTED);
    assert(dec.history[1].rank == 0);
    assert(dec.history[1].total == 1200);
}

static void test_history_and_stats_commands(void) {
    pr_connection conn;
    pr_connection_init(&conn, NULL, NULL);

    int fds[2];
    int sp_res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(sp_res == 0);
    conn.fd = fds[0];
    conn.connecting = 0;

    /* Gửi PR_GET_STATS */
    int st_res = pr_command_empty(&conn, PR_GET_STATS);
    assert(st_res == 0);

    /* Gửi PR_GET_HISTORY */
    int h_res = pr_command_page(&conn, PR_GET_HISTORY, 0, 10);
    assert(h_res == 0);

    /* Flush out buffer */
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(conn.fd, &writes);
    int p_res = pr_connection_process(&conn, &reads, &writes, 100.0);
    assert(p_res >= 0);

    /* Đọc frame từ socket phía server */
    uint8_t rx[PR_MAX_FRAME];
    ssize_t n = recv(fds[1], rx, sizeof(rx), 0);
    assert(n >= (ssize_t)PR_HEADER_SIZE);

    pr_header h;
    assert(pr_decode_header(rx, &h) == 1);
    assert(h.code == PR_GET_STATS);

    close(fds[0]);
    close(fds[1]);
}

int main(void) {
    test_stats_decoding_and_calculations();
    test_history_paging_decoding();
    test_history_and_stats_commands();

    printf("PASS: UI-07 Account stats calculations, history decoding, and wire commands\n");
    return 0;
}
