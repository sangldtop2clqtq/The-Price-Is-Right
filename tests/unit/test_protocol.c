#include "pr_io.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { unsigned count; size_t length; uint32_t last_id; } capture;
static int record(void *context, const pr_header *h, const uint8_t *payload) {
    capture *c = context;
    assert(h->kind == PR_REQUEST);
    assert(h->payload_length == c->length);
    if (c->length) { assert(payload[0] == 0xa5); assert(payload[c->length - 1] == 0xa5); }
    ++c->count;
    c->last_id = h->request_id;
    return 0;
}
static int reject_frame(void *context, const pr_header *h, const uint8_t *payload) {
    (void)context; (void)h; (void)payload; return -1;
}
static void test_primitives(void) {
    uint8_t bytes[64];
    pr_writer w;
    pr_writer_init(&w, bytes, sizeof(bytes));
    pr_put_u8(&w, 0xab);
    pr_put_u16(&w, 0x1234);
    pr_put_u32(&w, 0x89abcdefu);
    pr_put_u64(&w, UINT64_C(0x0123456789abcdef));
    pr_put_string(&w, "hello", 5);
    assert(w.ok && bytes[1] == 0x12 && bytes[2] == 0x34 && bytes[3] == 0x89);
    pr_reader r;
    pr_reader_init(&r, bytes, w.used);
    assert(pr_get_u8(&r) == 0xab);
    assert(pr_get_u16(&r) == 0x1234);
    assert(pr_get_u32(&r) == 0x89abcdefu);
    assert(pr_get_u64(&r) == UINT64_C(0x0123456789abcdef));
    size_t n;
    const uint8_t *s = pr_get_string(&r, &n, 5);
    assert(s && n == 5 && memcmp(s, "hello", 5) == 0 && pr_reader_done(&r));
    (void)pr_get_u8(&r);
    assert(!r.ok);
    pr_reader_init(&r, bytes + 15, 7);
    assert(!pr_get_string(&r, &n, 4) && !r.ok);
    pr_reader_init(&r, bytes + 15, 3);
    assert(!pr_get_string(&r, &n, 5) && !r.ok);
    pr_writer_init(&w, bytes, 1);
    pr_put_u16(&w, 42);
    assert(!w.ok && w.used == 0);
    pr_writer_init(&w, bytes, sizeof(bytes));
    pr_put_string(&w, "x", 65536);
    assert(!w.ok);
}
static void test_header(void) {
    const uint8_t expected[] = {0x50,0x52,1,0,0,0xf0,0,4,1,2,3,4,0xa5,0xa5,0xa5,0xa5};
    uint8_t frame[PR_MAX_FRAME], payload[4] = {0xa5,0xa5,0xa5,0xa5};
    pr_header h = {PR_REQUEST, PR_PING, 4, 0x01020304};
    size_t n = pr_encode_frame(frame, sizeof(frame), &h, payload);
    assert(n == sizeof(expected) && memcmp(frame, expected, n) == 0);
    pr_header decoded;
    assert(pr_decode_header(frame, &decoded));
    assert(decoded.code == PR_PING && decoded.request_id == h.request_id);
    assert(!pr_encode_frame(frame, 15, &h, payload));
    assert(!pr_encode_frame(frame, sizeof(frame), &h, NULL));
    h.request_id = 0;
    assert(!pr_encode_frame(frame, sizeof(frame), &h, payload));
    h.kind = PR_EVENT;
    assert(pr_encode_frame(frame, sizeof(frame), &h, payload));
    h.request_id = 1;
    assert(!pr_encode_frame(frame, sizeof(frame), &h, payload));
}
static void test_parser(void) {
    uint8_t frame[PR_MAX_FRAME], payload[PR_MAX_PAYLOAD];
    memset(payload, 0xa5, sizeof(payload));
    pr_header h = {PR_REQUEST, PR_PING, PR_MAX_PAYLOAD, 7};
    size_t n = pr_encode_frame(frame, sizeof(frame), &h, payload);
    pr_parser parser;
    capture c = {0, PR_MAX_PAYLOAD, 0};
    pr_parser_init(&parser);
    for (size_t i = 0; i < n; ++i) {
        assert(pr_parser_feed(&parser, frame + i, 1, record, &c) == 0);
        if (i + 1 < n) assert(c.count == 0);
    }
    assert(c.count == 1 && c.last_id == 7 && parser.used == 0);
    for (size_t split = 0; split <= PR_HEADER_SIZE + 4; ++split) {
        h.payload_length = 4;
        n = pr_encode_frame(frame, sizeof(frame), &h, payload);
        c = (capture){0, 4, 0};
        pr_parser_init(&parser);
        assert(pr_parser_feed(&parser, frame, split, record, &c) == 0);
        assert(pr_parser_feed(&parser, frame + split, n - split, record, &c) == 0);
        assert(c.count == 1 && parser.used == 0);
    }
    uint8_t joined[64];
    memcpy(joined, frame, n); memcpy(joined + n, frame, n); memcpy(joined + 2*n, frame, 5);
    c = (capture){0, 4, 0};
    pr_parser_init(&parser);
    assert(pr_parser_feed(&parser, joined, 2*n + 5, record, &c) == 0);
    assert(c.count == 2 && parser.used == 5);
    assert(pr_parser_feed(&parser, frame + 5, n - 5, record, &c) == 0);
    assert(c.count == 3);
    pr_parser_init(&parser);
    assert(pr_parser_feed(&parser, frame, n, reject_frame, NULL) == -1);
    /* Malformed header variants must fail without invoking a callback. */
    const size_t positions[] = {0, 2, 3, 6, 8};
    const uint8_t values[] = {0, 2, 3, 0x10, 0};
    for (size_t i = 0; i < sizeof(positions)/sizeof(positions[0]); ++i) {
        uint8_t bad[PR_HEADER_SIZE];
        memcpy(bad, frame, sizeof(bad));
        bad[positions[i]] = values[i];
        if (i == 3) bad[7] = 1; /* length 4097 */
        if (i == 4) memset(bad + 8, 0, 4);
        pr_parser_init(&parser);
        assert(pr_parser_feed(&parser, bad, sizeof(bad), record, &c) == -1);
    }
    h.payload_length = 0;
    n = pr_encode_frame(frame, sizeof(frame), &h, NULL);
    c = (capture){0, 0, 0};
    pr_parser_init(&parser);
    assert(pr_parser_feed(&parser, frame, n, record, &c) == 0 && c.count == 1);
}
static void test_output_queue(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pr_set_nonblocking(sockets[0]) == 0 && pr_set_nonblocking(sockets[1]) == 0);
    int size = 1024;
    assert(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0);
    uint8_t payload[PR_MAX_PAYLOAD];
    memset(payload, 0xa5, sizeof(payload));
    pr_header h = {PR_REQUEST, PR_PING, PR_MAX_PAYLOAD, 1};
    pr_tx_queue queue;
    pr_tx_init(&queue);
    for (unsigned i = 0; i < 63; ++i) {
        h.request_id = i + 1;
        assert(pr_tx_frame(&queue, &h, payload) == 0);
    }
    assert(pr_tx_frame(&queue, &h, payload) == -1); /* bounded queue */
    size_t before = queue.pending;
    assert(pr_tx_flush(&queue, sockets[0], 1) == 0 && queue.pending == before - 1);
    /* The socket buffer cannot hold the queue; sender must retain remaining bytes. */
    assert(pr_tx_flush(&queue, sockets[0], PR_TX_LIMIT) == 0 && queue.pending > 0);
    pr_parser parser;
    pr_parser_init(&parser);
    capture c = {0, PR_MAX_PAYLOAD, 0};
    for (unsigned iteration = 0; iteration < 100000 && c.count < 63; ++iteration) {
        uint8_t bytes[777];
        ssize_t got = recv(sockets[1], bytes, sizeof(bytes), 0);
        if (got > 0) assert(pr_parser_feed(&parser, bytes, (size_t)got, record, &c) == 0);
        else assert(got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        assert(pr_tx_flush(&queue, sockets[0], 1234) >= 0);
    }
    assert(c.count == 63 && c.last_id == 63 && queue.pending == 0 && parser.used == 0);
    assert(pr_tx_flush(&queue, sockets[0], 1) == 1);
    close(sockets[1]);
    h.payload_length = 0;
    assert(pr_tx_frame(&queue, &h, NULL) == 0);
    assert(pr_tx_flush(&queue, sockets[0], 64) == -1);
    pr_tx_clear(&queue);
    assert(queue.pending == 0 && !queue.head && !queue.tail);
    close(sockets[0]);
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    test_primitives(); test_header(); test_parser(); test_output_queue();
    puts("PASS: primitives, golden header, fragmented/coalesced frames, invalid headers, bounded/partial output");
    return 0;
}
