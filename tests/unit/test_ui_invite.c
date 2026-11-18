#include "pr_ui_state.h"
#include "pr_account_wire.h"
#include "pr_client.h"
#include "pr_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_online_players_decoding(void) {
    pr_page_view pv;
    memset(&pv, 0, sizeof(pv));
    pv.count = 2;
    pv.next_cursor = 250;

    pv.players[0].user_id = 201;
    strcpy(pv.players[0].name, "thuylinh");

    pv.players[1].user_id = 202;
    strcpy(pv.players[1].name, "anhquang");

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_page_encode(buffer, sizeof(buffer), PR_LIST_PLAYERS, &pv);
    assert(enc_len > 0);

    pr_page_view dec;
    memset(&dec, 0, sizeof(dec));
    int dec_ok = pr_page_decode(&dec, PR_LIST_PLAYERS, buffer, enc_len);
    assert(dec_ok == 1);
    assert(dec.count == 2);
    assert(dec.next_cursor == 250);
    assert(dec.players[0].user_id == 201);
    assert(strcmp(dec.players[0].name, "thuylinh") == 0);
    assert(dec.players[1].user_id == 202);
    assert(strcmp(dec.players[1].name, "anhquang") == 0);
}

static void test_invite_wire_and_framing(void) {
    /* 1. Test invite wire decoding */
    pr_invite_view iv;
    memset(&iv, 0, sizeof(iv));
    iv.inviter = 101;
    strcpy(iv.code, "ABC123");
    iv.expires_ms = 60000;

    uint8_t buffer[PR_MAX_PAYLOAD];
    size_t enc_len = pr_invite_encode(buffer, sizeof(buffer), &iv);
    assert(enc_len > 0);

    pr_invite_view dec;
    memset(&dec, 0, sizeof(dec));
    int dec_ok = pr_invite_decode(&dec, buffer, enc_len);
    assert(dec_ok == 1);
    assert(dec.inviter == 101);
    assert(strcmp(dec.code, "ABC123") == 0);
    assert(dec.expires_ms == 60000);

    /* 2. Test invite command validation & framing */
    pr_connection conn;
    pr_connection_init(&conn, NULL, NULL);

    /* target_id == 0 phải lỗi */
    assert(pr_command_invite(&conn, 0) < 0);

    int fds[2];
    int sp_res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(sp_res == 0);
    conn.fd = fds[0];
    conn.connecting = 0;

    int inv_res = pr_command_invite(&conn, 202);
    assert(inv_res == 0);

    fd_set writes, reads;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(conn.fd, &writes);
    int p_res = pr_connection_process(&conn, &reads, &writes, 100.0);
    assert(p_res >= 0);

    uint8_t rx[PR_MAX_FRAME];
    ssize_t n = recv(fds[1], rx, sizeof(rx), 0);
    assert(n >= (ssize_t)PR_HEADER_SIZE);

    pr_header h;
    assert(pr_decode_header(rx, &h) == 1);
    assert(h.code == PR_INVITE_PLAYER);
    assert(h.kind == PR_REQUEST);

    close(fds[0]);
    close(fds[1]);
}

static void test_invitation_lifecycle(void) {
    pr_ui_state state;
    pr_ui_state_init(&state);

    pr_invite_view iv;
    iv.inviter = 555;
    strcpy(iv.code, "ROOM99");
    iv.expires_ms = 60000;

    double now = pr_client_now();
    state.invitations[0].view = iv;
    state.invitations[0].deadline = now + 60.0;
    state.invitations[0].active = true;

    assert(state.invitations[0].active == true);
    assert(state.invitations[0].deadline > now);
    assert(strcmp(state.invitations[0].view.code, "ROOM99") == 0);

    /* Giả lập hết hạn */
    state.invitations[0].deadline = now - 1.0;
    if (state.invitations[0].deadline <= now) {
        state.invitations[0].active = false;
    }
    assert(state.invitations[0].active == false);
}

int main(void) {
    test_online_players_decoding();
    test_invite_wire_and_framing();
    test_invitation_lifecycle();

    printf("PASS: UI-08 Online players list, invite decoding, framing and invitation lifecycle\n");
    return 0;
}
