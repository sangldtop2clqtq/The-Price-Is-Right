#include "pr_ui_state.h"
#include "pr_ui.h"
#include "pr_game_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_format_vnd(void) {
    char out[64];

    pr_format_vnd(0, out, sizeof(out));
    assert(strcmp(out, "0 ₫") == 0);

    pr_format_vnd(500, out, sizeof(out));
    assert(strcmp(out, "500 ₫") == 0);

    pr_format_vnd(1000, out, sizeof(out));
    assert(strcmp(out, "1.000 ₫") == 0);

    pr_format_vnd(450000, out, sizeof(out));
    assert(strcmp(out, "450.000 ₫") == 0);

    pr_format_vnd(12500000, out, sizeof(out));
    assert(strcmp(out, "12.500.000 ₫") == 0);

    pr_format_vnd(1000000000ULL, out, sizeof(out));
    assert(strcmp(out, "1.000.000.000 ₫") == 0);
}

static void test_game_wire_decoding(void) {
    pr_game_view gv;
    memset(&gv, 0, sizeof(gv));
    gv.match_id = 9999;
    gv.round_no = 2;
    gv.type = PR_BASKET;
    gv.duration_ms = 30000;
    gv.remaining_ms = 25000;
    gv.count = 2;
    gv.item_count = 2;

    gv.participants[0].user_id = 101;
    strcpy(gv.participants[0].name, "user1");
    gv.participants[0].connected = 1;
    gv.totals[0] = 500;

    gv.participants[1].user_id = 102;
    strcpy(gv.participants[1].name, "user2");
    gv.participants[1].connected = 1;
    gv.totals[1] = 450;

    gv.items[0].product_id = 1;
    strcpy(gv.items[0].name, "Sản phẩm 1");
    strcpy(gv.items[0].image_key, "img_1");
    gv.items[0].quantity = 2;

    gv.items[1].product_id = 2;
    strcpy(gv.items[1].name, "Sản phẩm 2");
    strcpy(gv.items[1].image_key, "img_2");
    gv.items[1].quantity = 1;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t encoded_len = pr_game_encode(buffer, sizeof(buffer), PR_NEW_ROUND, &gv);
    assert(encoded_len > 0);

    pr_game_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_ok = pr_game_decode(&decoded, PR_NEW_ROUND, buffer, encoded_len);
    assert(dec_ok == 1);
    assert(decoded.match_id == 9999);
    assert(decoded.round_no == 2);
    assert(decoded.type == PR_BASKET);
    assert(decoded.item_count == 2);
    assert(strcmp(decoded.items[0].name, "Sản phẩm 1") == 0);
    assert(decoded.items[0].quantity == 2);
    assert(decoded.duration_ms == 30000);
    assert(decoded.remaining_ms == 25000);

    /* Test PR_GAME_SNAPSHOT encoding and decoding with totals and participants */
    gv.state = PR_RUNNING;
    gv.round_state = PR_ACCEPTING;
    encoded_len = pr_game_encode(buffer, sizeof(buffer), PR_GAME_SNAPSHOT, &gv);
    assert(encoded_len > 0);
    memset(&decoded, 0, sizeof(decoded));
    dec_ok = pr_game_decode(&decoded, PR_GAME_SNAPSHOT, buffer, encoded_len);
    assert(dec_ok == 1);
    assert(decoded.count == 2);
    assert(decoded.totals[0] == 500);
    assert(decoded.totals[1] == 450);
}

static void test_submit_guess_wire_framing(void) {
    pr_connection conn;
    pr_connection_init(&conn, NULL, NULL);

    /* Test bounds validation */
    assert(pr_command_guess(&conn, 0, 1, 400000) < 0);    /* match_id == 0 */
    assert(pr_command_guess(&conn, 100, 0, 400000) < 0);  /* round_no == 0 */
    assert(pr_command_guess(&conn, 100, 6, 400000) < 0);  /* round_no > 5 */
    assert(pr_command_guess(&conn, 100, 1, UINT64_C(1000000000001)) < 0); /* vượt 1 nghìn tỷ */

    /* Tạo socketpair kiểm tra framing */
    int fds[2];
    int sp_res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(sp_res == 0);
    conn.fd = fds[0];
    conn.connecting = 0;

    int req_res = pr_command_guess(&conn, 5555, 3, 450000);
    assert(req_res == 0);

    fd_set reads, writes;
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    uint8_t rx_buf[256];
    ssize_t n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)(PR_HEADER_SIZE + 17));

    pr_header h;
    assert(pr_decode_header(rx_buf, &h) == 1);
    assert(h.code == PR_SUBMIT_GUESS);
    assert(h.payload_length == 17);

    pr_reader r;
    pr_reader_init(&r, rx_buf + PR_HEADER_SIZE, 17);
    uint64_t m_id = pr_get_u64(&r);
    uint8_t r_no = pr_get_u8(&r);
    uint64_t amt = pr_get_u64(&r);
    assert(r.ok);
    assert(m_id == 5555);
    assert(r_no == 3);
    assert(amt == 450000);

    close(fds[0]);
    close(fds[1]);
    conn.fd = -1;
}

static void test_spectator_guess_monitoring(void) {
    pr_ui_state s;
    pr_ui_state_init(&s);
    pr_ui_state_init_mock(&s);

    /* Verify mock has spectator names and submitted amounts */
    assert(s.spectator_name_count > 0);
    assert(s.spectator_names[0][0] != '\0');
    assert(s.player_submitted[0] == true);
    assert(s.player_guess_amounts[0] > 0);

    /* Verify encoding and decoding of PR_SUBMIT_GUESS event frame */
    uint8_t ev_buf[17];
    pr_writer w;
    pr_writer_init(&w, ev_buf, sizeof(ev_buf));
    pr_put_u64(&w, 101);
    pr_put_u8(&w, 2);
    pr_put_u64(&w, 750000);
    assert(w.ok);

    pr_reader r;
    pr_reader_init(&r, ev_buf, sizeof(ev_buf));
    uint64_t uid = pr_get_u64(&r);
    uint8_t rnd = pr_get_u8(&r);
    uint64_t amt = pr_get_u64(&r);
    assert(r.ok);
    assert(uid == 101);
    assert(rnd == 2);
    assert(amt == 750000);
}

int main(void) {
    test_format_vnd();
    test_game_wire_decoding();
    test_submit_guess_wire_framing();
    test_spectator_guess_monitoring();
    printf("PASS: UI-05 Gameplay VND formatting, NEW_ROUND decoding, SUBMIT_GUESS wire framing and spectator monitoring\n");
    return 0;
}
