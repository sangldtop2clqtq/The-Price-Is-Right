#include "pr_ui_state.h"
#include "pr_ui.h"
#include "pr_room_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_vietnamese_status_messages(void) {
    assert(strcmp(pr_status_to_vietnamese(PR_OK), "Thành công") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_AUTH_FAILED), "Tên đăng nhập hoặc mật khẩu không chính xác") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_USER_EXISTS), "Tên tài khoản này đã được sử dụng") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_ALREADY_ONLINE), "Tài khoản này đang đăng nhập ở nơi khác") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_ROOM_NOT_FOUND), "Mã phòng không tồn tại") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_ROOM_FULL), "Phòng đã đủ số lượng người chơi") == 0);
    assert(strcmp(pr_status_to_vietnamese(PR_NOT_AUTHENTICATED), "Bạn chưa đăng nhập") == 0);
    assert(strcmp(pr_status_to_vietnamese(9999), "Lỗi không xác định từ máy chủ") == 0);
}

static void test_room_code_normalization_and_validation(void) {
    /* Test helper chuẩn hóa mã phòng tương tự pr_app_do_join_room */
    const char *inputs[] = { "  abc123  ", "xyz789", "  RoOm99 ", "123456", "aB9cDe" };
    const char *expected[] = { "ABC123", "XYZ789", "ROOM99", "123456", "AB9CDE" };

    for (size_t k = 0; k < sizeof(inputs)/sizeof(inputs[0]); ++k) {
        const char *code = inputs[k];
        while (*code == ' ' || *code == '\t') code++;
        char clean[8];
        memset(clean, 0, sizeof(clean));
        for (size_t i = 0; i < 6 && code[i] && code[i] != ' ' && code[i] != '\t'; ++i) {
            char ch = code[i];
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            clean[i] = ch;
        }
        clean[6] = '\0';
        assert(strcmp(clean, expected[k]) == 0);
        assert(pr_valid_room_code(clean));
    }

    /* Các mã không hợp lệ */
    assert(!pr_valid_room_code("AB"));        /* Quá ngắn */
    assert(!pr_valid_room_code("ABC1234"));   /* Quá dài */
    assert(!pr_valid_room_code("AB-123"));    /* Ký tự đặc biệt */
    assert(!pr_valid_room_code(""));          /* Rỗng */
}

static void test_auth_commands_wire_framing(void) {
    pr_connection conn;
    pr_connection_init(&conn, NULL, NULL);

    /* Test auth bounds */
    assert(pr_command_auth(&conn, PR_LOGIN, "ab", "12345678") < 0); /* User quá ngắn (2 < 3) */
    assert(pr_command_auth(&conn, PR_LOGIN, "validuser", "short") < 0); /* Pass quá ngắn (5 < 8) */
    assert(pr_command_auth(&conn, PR_CREATE_ROOM, "user", "12345678") < 0); /* Sai mã code */

    /* Tạo kết nối giả bằng socketpair để kiểm tra framing */
    int fds[2];
    int sp_res = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(sp_res == 0);
    conn.fd = fds[0];
    conn.connecting = 0;

    /* Gửi auth request hợp lệ */
    int req_res = pr_command_auth(&conn, PR_LOGIN, "alice", "supersecret123");
    assert(req_res == 0);
    assert(conn.tx.pending > 0);

    /* Đọc frame từ socket bên kia */
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(conn.fd, &writes);
    int proc_res = pr_connection_process(&conn, &reads, &writes, 100.0);
    assert(proc_res == 0);

    uint8_t rx_buf[256];
    ssize_t n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n >= (ssize_t)PR_HEADER_SIZE);

    pr_header h;
    int dec_ok = pr_decode_header(rx_buf, &h);
    assert(dec_ok == 1);
    assert(h.kind == PR_REQUEST);
    assert(h.code == PR_LOGIN);

    /* Gửi PR_CREATE_ROOM */
    req_res = pr_command_empty(&conn, PR_CREATE_ROOM);
    assert(req_res == 0);
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n == (ssize_t)PR_HEADER_SIZE);
    dec_ok = pr_decode_header(rx_buf, &h);
    assert(dec_ok == 1);
    assert(h.code == PR_CREATE_ROOM);
    assert(h.payload_length == 0);

    /* Gửi PR_JOIN_ROOM */
    req_res = pr_command_join(&conn, "XYZ999");
    assert(req_res == 0);
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(conn.fd, &writes);
    pr_connection_process(&conn, &reads, &writes, 100.0);

    n = recv(fds[1], rx_buf, sizeof(rx_buf), 0);
    assert(n > (ssize_t)PR_HEADER_SIZE);
    dec_ok = pr_decode_header(rx_buf, &h);
    assert(dec_ok == 1);
    assert(h.code == PR_JOIN_ROOM);

    /* Dọn dẹp */
    close(fds[0]);
    close(fds[1]);

    conn.fd = -1;
}


int main(void) {
    test_vietnamese_status_messages();
    test_room_code_normalization_and_validation();
    test_auth_commands_wire_framing();
    printf("PASS: UI-03 Auth and Lobby validation, Vietnamese error mapping, and command wire framing\n");
    return 0;
}
