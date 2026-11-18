#include "pr_ui.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int setup_test_server(uint16_t *port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(server_fd >= 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = 0; /* Ephemeral port */

    int res = bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(res == 0);

    socklen_t len = sizeof(addr);
    res = getsockname(server_fd, (struct sockaddr *)&addr, &len);
    assert(res == 0);
    *port = ntohs(addr.sin_port);

    res = listen(server_fd, 1);
    assert(res == 0);

    return server_fd;
}

void pr_app_disconnect(pr_app *app, const char *reason) {
    if (!app) return;
    pr_net_source_detach(app);
    if (app->state.net.fd >= 0) {
        pr_connection_close(&app->state.net);
    }
    app->state.conn_status = PR_CONN_IDLE;
    (void)reason;
}

void pr_app_show_notification(pr_app *app, const char *msg, const char *level) {
    (void)app; (void)msg; (void)level;
}

void pr_app_update_conn_badge(pr_app *app) {
    (void)app;
}

static int mock_pong_received = 0;
static uint32_t mock_pong_nonce = 0;

static int test_client_callback(void *context, const pr_header *h, const uint8_t *payload, uint64_t tag) {
    (void)context;
    (void)tag;
    if (h->kind == PR_RESPONSE && h->code == PR_PING) {
        pr_reader r;
        pr_reader_init(&r, payload, h->payload_length);
        uint16_t status = pr_get_u16(&r);
        assert(status == PR_OK);
        mock_pong_nonce = pr_get_u32(&r);
        mock_pong_received = 1;
    }
    return 0;
}

static void test_glib_source_ping_pong(void) {
    uint16_t port = 0;
    int srv_fd = setup_test_server(&port);
    assert(srv_fd >= 0 && port > 0);

    pr_app app;
    memset(&app, 0, sizeof(app));
    pr_ui_state_init(&app.state);
    snprintf(app.state.server_ip, sizeof(app.state.server_ip), "127.0.0.1");
    app.state.server_port = port;

    pr_connection_init(&app.state.net, test_client_callback, &app);
    double now = pr_client_now();
    int res = pr_connection_open(&app.state.net, "127.0.0.1", port, now);
    assert(res == 0);

    /* Gắn GSource vào default main context */
    pr_net_source_attach(&app);
    assert(app.net_source_id != 0);

    /* Chấp nhận kết nối trên server socket */
    int client_fd = accept(srv_fd, NULL, NULL);
    assert(client_fd >= 0);

    /* Chạy một vài iteration để xử lý non-blocking connect */
    for (int i = 0; i < 10; ++i) {
        g_main_context_iteration(NULL, FALSE);
    }
    assert(app.state.net.connecting == 0);

    /* Gửi PING từ client */
    mock_pong_received = 0;
    uint32_t send_nonce = 0x12345678;
    res = pr_command_ping(&app.state.net, send_nonce);
    assert(res == 0);
    pr_net_source_notify_send(&app);

    /* Flush ghi trên client */
    g_main_context_iteration(NULL, FALSE);

    /* Đọc frame PING trên server socket */
    uint8_t buf[64];
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
    assert(n >= (ssize_t)PR_HEADER_SIZE);

    pr_header h;
    res = pr_decode_header(buf, &h);
    assert(res == 1);
    assert(h.kind == PR_REQUEST);
    assert(h.code == PR_PING);

    /* Server đóng gói PONG response và gửi lại */
    uint8_t payload[8];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload));
    pr_put_u16(&w, PR_OK);
    pr_put_u32(&w, send_nonce);

    pr_header pong_h = {
        PR_RESPONSE,
        PR_PING,
        (uint16_t)w.used,
        h.request_id
    };

    uint8_t frame[64];
    size_t frame_len = pr_encode_frame(frame, sizeof(frame), &pong_h, payload);
    assert(frame_len > 0);

    ssize_t sent = send(client_fd, frame, frame_len, 0);
    assert(sent == (ssize_t)frame_len);

    /* Main loop client nhận và dispatch */
    for (int i = 0; i < 10 && !mock_pong_received; ++i) {
        g_main_context_iteration(NULL, TRUE);
    }

    assert(mock_pong_received == 1);
    assert(mock_pong_nonce == send_nonce);

    /* Cleanup */
    pr_app_disconnect(&app, NULL);
    assert(app.net_source_id == 0);
    assert(app.state.net.fd < 0);

    close(client_fd);
    close(srv_fd);
}

int main(void) {
    test_glib_source_ping_pong();
    printf("PASS: GLib GSource adapter integration (non-blocking connect, notify send, PING-PONG framing, teardown)\n");
    return 0;
}
