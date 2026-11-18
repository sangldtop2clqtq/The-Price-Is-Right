#include "pr_ui_state.h"
#include "pr_ui.h"
#include "pr_room_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_room_view_decoding_and_state(void) {
    pr_room_view view;
    memset(&view, 0, sizeof(view));
    strcpy(view.code, "ROOM01");
    view.state = PR_WAITING;
    view.self_role = PR_PLAYER;
    view.host_id = 1001;
    view.revision = 1;
    view.player_count = 2;
    view.spectator_count = 3;


    view.players[0].user_id = 1001;
    strcpy(view.players[0].name, "host_player");
    view.players[0].ready = 1;
    view.players[0].connected = 1;

    view.players[1].user_id = 1002;
    strcpy(view.players[1].name, "second_player");
    view.players[1].ready = 0;
    view.players[1].connected = 1;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t encoded_len = pr_room_encode(buffer, sizeof(buffer), &view);
    assert(encoded_len > 0);

    pr_room_view decoded;
    memset(&decoded, 0, sizeof(decoded));
    int dec_res = pr_room_decode(&decoded, buffer, encoded_len);
    assert(dec_res == 1);
    assert(strcmp(decoded.code, "ROOM01") == 0);
    assert(decoded.player_count == 2);
    assert(decoded.spectator_count == 3);
    assert(decoded.players[0].ready == 1);
    assert(decoded.players[1].ready == 0);
}

static void test_room_commands_wire_framing(void) {
    pr_connection conn;
    pr_connection_init(&conn, NULL, NULL);

    int fds[2];
    int sp_res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(sp_res == 0);
    conn.fd = fds[0];
    conn.connecting = 0;

    fd_set reads, writes;
    uint8_t rx_buf[256];
    pr_header h;

    /* 1. Gửi lệnh PR_SET_READY = 1 */
    int req_res = pr_command_ready(&conn, 1);
    assert(req_res == 0);
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    ssize_t n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)(PR_HEADER_SIZE + 1));
    assert(pr_decode_header(rx_buf, &h) == 1);
    assert(h.code == PR_SET_READY);
    assert(rx_buf[PR_HEADER_SIZE] == 1);

    /* 2. Gửi lệnh PR_SET_READY = 0 */
    req_res = pr_command_ready(&conn, 0);
    assert(req_res == 0);
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)(PR_HEADER_SIZE + 1));
    assert(pr_decode_header(rx_buf, &h) == 1);
    assert(h.code == PR_SET_READY);
    assert(rx_buf[PR_HEADER_SIZE] == 0);

    /* 3. Gửi lệnh PR_START_GAME */
    req_res = pr_command_empty(&conn, PR_START_GAME);
    assert(req_res == 0);
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)PR_HEADER_SIZE);
    assert(pr_decode_header(rx_buf, &h) == 1);
    assert(h.code == PR_START_GAME);

    /* 4. Gửi lệnh PR_LEAVE_ROOM */
    req_res = pr_command_empty(&conn, PR_LEAVE_ROOM);
    assert(req_res == 0);
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)PR_HEADER_SIZE);
    assert(pr_decode_header(rx_buf, &h) == 1);
    assert(h.code == PR_LEAVE_ROOM);

    close(fds[0]);
    close(fds[1]);
    conn.fd = -1;
}

int main(void) {
    test_room_view_decoding_and_state();
    test_room_commands_wire_framing();
    printf("PASS: UI-04 Room Waiting roster, ready toggles, start conditions and leave commands\n");
    return 0;
}
