#include "pr_client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

double pr_client_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
void pr_connection_init(pr_connection *c, pr_client_callback callback, void *context) {
    memset(c, 0, sizeof(*c));
    c->fd = -1; c->next_id = 1; c->callback = callback; c->context = context;
}
void pr_connection_close(pr_connection *c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1; c->connecting = 0; c->outstanding = 0;
    pr_tx_clear(&c->tx);
    pr_wipe(c->pending, sizeof(c->pending));
    pr_wipe(&c->parser, sizeof(c->parser));
}
static int fail(pr_connection *c, const char *error) {
    c->error = error;
    pr_connection_close(c);
    return -1;
}
int pr_connection_open(pr_connection *c, const char *host, uint16_t port, double now) {
    if (c->fd >= 0) return -1;
    c->error = NULL;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons(port);
    if (!port || inet_pton(AF_INET, host, &address.sin_addr) != 1) return fail(c, "invalid numeric IPv4/port");
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0 || c->fd >= FD_SETSIZE || pr_set_nonblocking(c->fd) < 0) return fail(c, "socket setup failed");
    if (connect(c->fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (errno != EINPROGRESS) return fail(c, "connect failed");
        c->connecting = 1; c->connect_deadline = now + 5.0;
    }
    return 0;
}
int pr_connection_request(pr_connection *c, uint16_t code, const uint8_t *payload, size_t length,
                          uint64_t tag, double now, uint32_t *request_id) {
    if (c->fd < 0 || length > PR_MAX_PAYLOAD || (length && !payload) || c->outstanding >= 64) return -1;
    size_t slot = 0;
    while (c->pending[slot].id) ++slot;
    uint32_t id;
    int collision;
    do {
        id = c->next_id++;
        collision = id == 0;
        for (size_t i = 0; i < 64; ++i) if (c->pending[i].id == id) collision = 1;
    } while (collision);
    pr_header h = {PR_REQUEST, code, (uint16_t)length, id};
    if (pr_tx_frame(&c->tx, &h, payload) < 0) return -1;
    c->pending[slot] = (pr_pending){id, code, tag, now + 15.0};
    ++c->outstanding;
    if (request_id) *request_id = id;
    return 0;
}
static int dispatch(void *context, const pr_header *h, const uint8_t *payload) {
    pr_connection *c = context;
    uint64_t tag = 0;
    if (h->kind == PR_RESPONSE) {
        size_t slot = 0;
        while (slot < 64 && c->pending[slot].id != h->request_id) ++slot;
        if (slot == 64 || c->pending[slot].code != h->code) return -1;
        tag = c->pending[slot].tag;
        memset(&c->pending[slot], 0, sizeof(c->pending[slot]));
        --c->outstanding;
    } else if (h->kind != PR_EVENT) return -1;
    return c->callback ? c->callback(c->context, h, payload, tag) : 0;
}
void pr_connection_watch(pr_connection *c, fd_set *reads, fd_set *writes, int *max_fd) {
    if (c->fd < 0) return;
    FD_SET(c->fd, reads);
    if (c->connecting || c->tx.pending) FD_SET(c->fd, writes);
    if (c->fd > *max_fd) *max_fd = c->fd;
}
int pr_connection_tick(pr_connection *c, double now) {
    if (c->fd < 0) return -1;
    if (c->connecting && now >= c->connect_deadline) return fail(c, "connect timeout");
    for (size_t i = 0; i < 64; ++i)
        if (c->pending[i].id && now >= c->pending[i].deadline) return fail(c, "response timeout");
    return 0;
}
int pr_connection_process(pr_connection *c, const fd_set *reads, const fd_set *writes, double now) {
    if (pr_connection_tick(c, now) < 0) return -1;
    int fd = c->fd;
    if (c->connecting) {
        if (!FD_ISSET(fd, reads) && !FD_ISSET(fd, writes)) return 0;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) return fail(c, "connect failed");
        c->connecting = 0;
    }
    if (FD_ISSET(fd, writes) && pr_tx_flush(&c->tx, fd, 65536) < 0) return fail(c, "send failed");
    if (FD_ISSET(fd, reads)) {
        uint8_t bytes[8192];
        size_t budget = 65536;
        while (budget) {
            size_t size = budget < sizeof(bytes) ? budget : sizeof(bytes);
            ssize_t n = recv(fd, bytes, size, 0);
            if (n == 0) return fail(c, "server disconnected");
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return fail(c, "receive failed");
            }
            int result = pr_parser_feed(&c->parser, bytes, (size_t)n, dispatch, c);
            pr_wipe(bytes, (size_t)n);
            if (result < 0) return fail(c, "invalid response/event");
            budget -= (size_t)n;
        }
    }
    return 0;
}
