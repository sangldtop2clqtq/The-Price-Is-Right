#include "pr_io.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>

struct pr_chunk { struct pr_chunk *next; size_t length, offset; uint8_t bytes[]; };
int pr_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
void pr_tx_init(pr_tx_queue *q) { *q = (pr_tx_queue){0}; }
void pr_tx_clear(pr_tx_queue *q) {
    while (q->head) {
        pr_chunk *next = q->head->next;
        pr_wipe(q->head->bytes, q->head->length);
        free(q->head); q->head = next;
    }
    pr_tx_init(q);
}
int pr_tx_frame(pr_tx_queue *q, const pr_header *h, const uint8_t *payload) {
    size_t n = PR_HEADER_SIZE + h->payload_length;
    if (n > PR_MAX_FRAME || n > PR_TX_LIMIT - q->pending) return -1;
    pr_chunk *chunk = malloc(sizeof(*chunk) + n);
    if (!chunk) return -1;
    chunk->length = pr_encode_frame(chunk->bytes, n, h, payload);
    if (!chunk->length) { free(chunk); return -1; }
    chunk->next = NULL;
    chunk->offset = 0;
    if (q->tail) q->tail->next = chunk; else q->head = chunk;
    q->tail = chunk;
    q->pending += n;
    return 0;
}
int pr_tx_flush(pr_tx_queue *q, int fd, size_t budget) {
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) return -1;
#endif
    while (q->head && budget) {
        pr_chunk *chunk = q->head;
        size_t n = chunk->length - chunk->offset;
        if (n > budget) n = budget;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t sent = send(fd, chunk->bytes + chunk->offset, n, flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (!sent) return -1;
        chunk->offset += (size_t)sent;
        q->pending -= (size_t)sent;
        budget -= (size_t)sent;
        if (chunk->offset == chunk->length) {
            q->head = chunk->next;
            if (!q->head) q->tail = NULL;
            pr_wipe(chunk->bytes, chunk->length);
            free(chunk);
        }
    }
    return q->head ? 0 : 1;
}
