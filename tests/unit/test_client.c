#include "pr_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { unsigned responses, events; uint64_t tag; } capture;
static int received(void *context, const pr_header *h, const uint8_t *payload, uint64_t tag) {
    (void)payload;
    capture *out = context;
    if (h->kind == PR_EVENT) ++out->events;
    else { ++out->responses; out->tag = tag; }
    return 0;
}
static void setup(pr_connection *c, int sockets[2], capture *out) {
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pr_set_nonblocking(sockets[0]) == 0 && pr_set_nonblocking(sockets[1]) == 0);
    pr_connection_init(c, received, out);
    c->fd = sockets[0];
}
static int reply(pr_connection *c, int peer, uint8_t kind, uint16_t code, uint32_t id, double now) {
    uint8_t bytes[12];
    pr_header h = {kind, code, 0, id};
    assert(pr_encode_frame(bytes, sizeof(bytes), &h, NULL) == 12);
    assert(write(peer, bytes, sizeof(bytes)) == 12);
    fd_set reads, writes;
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(c->fd, &reads);
    return pr_connection_process(c, &reads, &writes, now);
}
int main(void) {
    pr_connection c;
    capture out = {0};
    int sockets[2];
    setup(&c, sockets, &out);
    uint32_t first, second;
    assert(pr_connection_request(&c, PR_PING, NULL, 0, 42, 1, &first) == 0);
    assert(pr_connection_request(&c, PR_PING, NULL, 4097, 0, 1, NULL) == -1);
    assert(reply(&c, sockets[1], PR_EVENT, PR_ROOM_UPDATE, 0, 2) == 0);
    assert(out.events == 1 && c.outstanding == 1);
    assert(pr_connection_request(&c, PR_PING, NULL, 0, 99, 14, &second) == 0);
    assert(reply(&c, sockets[1], PR_RESPONSE, PR_PING, first, 15) == 0);
    assert(out.responses == 1 && out.tag == 42 && c.outstanding == 1);
    assert(pr_connection_tick(&c, 16) == 0); /* second request has its own deadline */
    assert(pr_connection_tick(&c, 29) == -1 && !strcmp(c.error, "response timeout"));
    assert(c.fd == -1 && c.outstanding == 0 && c.tx.pending == 0);
    close(sockets[1]);

    setup(&c, sockets, &out);
    c.next_id = UINT32_MAX;
    assert(pr_connection_request(&c, PR_PING, NULL, 0, 0, 1, &first) == 0 && first == UINT32_MAX);
    assert(pr_connection_request(&c, PR_PING, NULL, 0, 0, 1, &second) == 0 && second == 1);
    assert(reply(&c, sockets[1], PR_RESPONSE, PR_LOGIN, first, 2) == -1);
    assert(c.fd == -1 && c.outstanding == 0);
    close(sockets[1]);

    setup(&c, sockets, &out);
    assert(pr_connection_request(&c, PR_PING, NULL, 0, 0, 1, NULL) == 0);
    close(sockets[1]);
    fd_set reads, writes;
    FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(c.fd, &writes);
    /* Default SIGPIPE handler is unchanged: the I/O library must prevent process death. */
    assert(pr_connection_process(&c, &reads, &writes, 2) == -1 && c.fd == -1);
    pr_connection_close(&c);
    puts("PASS: event/response separation, per-request deadlines, ID wrap, invalid replies and disconnect cleanup");
    return 0;
}
