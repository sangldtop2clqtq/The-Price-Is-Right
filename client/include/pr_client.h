#ifndef PR_CLIENT_H
#define PR_CLIENT_H
#include "pr_io.h"
#include <sys/select.h>

typedef int (*pr_client_callback)(void *, const pr_header *, const uint8_t *, uint64_t tag);
typedef struct {
    uint32_t id;
    uint16_t code;
    uint64_t tag;
    double deadline;
} pr_pending;
typedef struct {
    int fd, connecting;
    const char *error;
    double connect_deadline;
    uint32_t next_id;
    size_t outstanding;
    pr_pending pending[64];
    pr_parser parser;
    pr_tx_queue tx;
    pr_client_callback callback;
    void *context;
} pr_connection;

/* No UI, printf, GLib or global signal handlers. All calls on the owning thread.
 * Callback borrows payload until return; may enqueue requests, must not close/reenter.
 * Request/event callbacks return 0 to accept, -1 to reject malformed application data. */
void pr_connection_init(pr_connection *, pr_client_callback, void *);
int pr_connection_open(pr_connection *, const char *ipv4, uint16_t port, double now);
void pr_connection_close(pr_connection *);
int pr_connection_request(pr_connection *, uint16_t code, const uint8_t *, size_t,
                          uint64_t tag, double now, uint32_t *request_id);
void pr_connection_watch(pr_connection *, fd_set *, fd_set *, int *max_fd);
int pr_connection_tick(pr_connection *, double now);
int pr_connection_process(pr_connection *, const fd_set *, const fd_set *, double now);
double pr_client_now(void);

int pr_command_ping(pr_connection *, uint32_t nonce);
int pr_command_auth(pr_connection *, uint16_t code, const char *username, const char *password);
int pr_command_join(pr_connection *, const char *code);
int pr_command_ready(pr_connection *, uint8_t ready);
int pr_command_empty(pr_connection *, uint16_t code);
int pr_command_guess(pr_connection *, uint64_t match_id, uint8_t round_no, uint64_t amount);
int pr_command_page(pr_connection *, uint16_t code, uint64_t cursor, uint16_t limit);
int pr_command_invite(pr_connection *, uint64_t target_user_id);
#endif
