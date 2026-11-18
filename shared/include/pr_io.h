#ifndef PR_IO_H
#define PR_IO_H
#include "pr_protocol.h"

#define PR_TX_LIMIT (256u * 1024u)
typedef struct pr_chunk pr_chunk;
typedef struct { pr_chunk *head, *tail; size_t pending; } pr_tx_queue;

int pr_set_nonblocking(int);
void pr_tx_init(pr_tx_queue *);
void pr_tx_clear(pr_tx_queue *);
int pr_tx_frame(pr_tx_queue *, const pr_header *, const uint8_t *);
/* 1: drained, 0: would block or budget exhausted, -1: fatal send error. */
int pr_tx_flush(pr_tx_queue *, int, size_t budget);
#endif
