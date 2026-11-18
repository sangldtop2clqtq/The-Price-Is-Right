#include "pr_server.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define IO_BUDGET (64u * 1024u)
static volatile sig_atomic_t stopping;
static void stop_server(int signal_number) { (void)signal_number; stopping = 1; }
void pr_close_client(pr_client *c) {
    if (c->fd < 0) return;
    const char *room = c->room_index >= 0 ? c->app->rooms[c->room_index].code : NULL;
    pr_log_event("disconnect", c->generation, c->user_id, room, 0, 0, 0, 0,
                 c->close_reason ? c->close_reason : "peer_closed");
    close(c->fd);
    c->fd = -1;
    pr_room_detach(c);
    c->user_id = 0;
    c->auth_busy = 0;
    c->drop = 0;
    c->read_closed = 0; c->partial_since = 0; c->request_started = 0; c->close_reason = NULL;
    sodium_memzero(c->display_name, sizeof(c->display_name));
    pr_tx_clear(&c->tx);
    sodium_memzero(&c->parser, sizeof(c->parser));
    while (c->requests_head) {
        pr_request_node *node = c->requests_head;
        c->requests_head = node->next;
        sodium_memzero(node, sizeof(*node) + node->header.payload_length);
        free(node);
    }
    c->requests_tail = NULL;
    c->request_bytes = 0;
}
int pr_reply(pr_client *c, const pr_header *h, uint16_t status,
             const uint8_t *body, size_t length) {
    uint8_t payload[PR_MAX_PAYLOAD];
    if (length > PR_MAX_PAYLOAD - 2) return -1;
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload));
    pr_put_u16(&w, status);
    if (status == PR_OK && length) { memcpy(payload + w.used, body, length); w.used += length; }
    pr_header reply = {PR_RESPONSE, h->code, (uint16_t)w.used, h->request_id};
    int result = pr_tx_frame(&c->tx, &reply, payload);
    pr_log_event("response", c->generation, c->user_id,
        c->room_index >= 0 ? c->app->rooms[c->room_index].code : NULL,
        h->request_id, h->code, status, pr_server_now() - c->request_started,
        result < 0 ? "tx_queue_limit" : "queued");
    if (result < 0) c->close_reason = "tx_queue_limit";
    return result;
}
static int on_request(void *context, const pr_header *h, const uint8_t *payload) {
    pr_client *c = context;
    if (h->kind != PR_REQUEST) { c->close_reason = "invalid_direction"; return -1; }
    size_t size = sizeof(pr_request_node) + h->payload_length;
    if (size > IO_BUDGET - c->request_bytes) { c->close_reason = "request_queue_limit"; return -1; }
    pr_request_node *node = malloc(size);
    if (!node) { c->close_reason = "allocation_failed"; return -1; }
    node->header = *h;
    node->received_at = pr_server_now();
    c->partial_since = 0;
    node->next = NULL;
    if (h->payload_length) memcpy(node->payload, payload, h->payload_length);
    if (c->requests_tail) c->requests_tail->next = node; else c->requests_head = node;
    c->requests_tail = node;
    c->request_bytes += size;
    return 0;
}
static void process_requests(pr_app *app, size_t slot) {
    pr_client *c = &app->clients[slot];
    if (c->drop && c->fd >= 0) { pr_close_client(c); return; }
    if (c->fd >= 0 && c->partial_since && pr_server_now() - c->partial_since >= app->frame_timeout) {
        c->close_reason = "frame_timeout"; pr_close_client(c); return;
    }
    if (c->fd >= 0 && c->read_closed && !c->requests_head && !c->auth_busy && !c->tx.pending) {
        c->close_reason = "peer_eof"; pr_close_client(c); return;
    }
    for (unsigned count = 0; count < 32 && c->fd >= 0 && !c->drop && !c->auth_busy && c->requests_head; ++count) {
        pr_request_node *node = c->requests_head;
        c->requests_head = node->next;
        if (!c->requests_head) c->requests_tail = NULL;
        size_t size = sizeof(*node) + node->header.payload_length;
        c->request_bytes -= size;
        const pr_header *h = &node->header;
        c->request_started = node->received_at;
        int result;
        if (h->code == PR_REGISTER || h->code == PR_LOGIN || h->code == PR_LOGOUT)
            result = pr_auth_handle(app, slot, h, node->payload);
        else if (h->code == PR_GET_HISTORY || h->code == PR_GET_STATS || h->code == PR_LIST_PLAYERS || h->code == PR_INVITE_PLAYER)
            result = pr_account_handle(app, slot, h, node->payload);
        else if (h->code == PR_CREATE_ROOM || h->code == PR_JOIN_ROOM || h->code == PR_LEAVE_ROOM ||
                 h->code == PR_SET_READY || h->code == PR_START_GAME || h->code == PR_SUBMIT_GUESS)
            result = pr_room_handle(app, slot, h, node->payload);
        else if (h->code == PR_PING) {
            if (h->payload_length != 4) result = pr_reply(c, h, PR_INVALID_MESSAGE, NULL, 0);
            else result = pr_reply(c, h, PR_OK, node->payload, 4);
        } else {
            uint16_t status = !pr_is_request_code(h->code) ? PR_INVALID_MESSAGE :
                              !c->user_id ? PR_NOT_AUTHENTICATED : PR_NOT_IMPLEMENTED;
            result = pr_reply(c, h, status, NULL, 0);
        }
        sodium_memzero(node, size);
        free(node);
        if (result < 0) { pr_close_client(c); break; }
    }
}
static int read_client(pr_client *c) {
    uint8_t bytes[8192];
    size_t budget = IO_BUDGET;
    while (budget) {
        size_t n = budget < sizeof(bytes) ? budget : sizeof(bytes);
        ssize_t got = recv(c->fd, bytes, n, 0);
        if (got == 0) {
            if (c->parser.used) { c->close_reason = "truncated_frame"; return -1; }
            c->read_closed = 1; return 0;
        }
        if (got < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            c->close_reason = "receive_error"; return -1;
        }
        int parsed = pr_parser_feed(&c->parser, bytes, (size_t)got, on_request, c);
        sodium_memzero(bytes, (size_t)got);
        if (parsed < 0) {
            if (!c->close_reason) c->close_reason = "invalid_frame";
            return -1;
        }
        if (c->parser.used && !c->partial_since) c->partial_since = pr_server_now();
        sodium_memzero(c->parser.bytes + c->parser.used, sizeof(c->parser.bytes) - c->parser.used);
        budget -= (size_t)got;
    }
    return 0;
}
static int duration_config(const char *key, uint32_t fallback, uint32_t *out) {
    const char *value = getenv(key);
    if (!value || !*value) { *out = fallback; return 1; }
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || *end || parsed < 50 || parsed > 60000) {
        fprintf(stderr, "Invalid %s (50..60000 ms)\n", key); return 0;
    }
    *out = (uint32_t)parsed; return 1;
}
int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    long port = 6666;
    if (argc > 3) { fprintf(stderr, "Usage: %s [IPv4-address [port]]\n", argv[0]); return 2; }
    if (argc > 1) host = argv[1];
    if (argc > 2) {
        char *end;
        port = strtol(argv[2], &end, 10);
        if (!*argv[2] || *end || port < 1 || port > 65535) return 2;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    if (sodium_init() < 0) { fprintf(stderr, "libsodium initialization failed\n"); return 1; }
    if (pr_log_open() < 0) { perror("log setup"); return 1; }
    pr_app app;
    memset(&app, 0, sizeof(app));
    if (!duration_config("PR_GAME_SINGLE_MS", 20000, &app.single_ms) ||
        !duration_config("PR_GAME_BASKET_MS", 30000, &app.basket_ms) ||
        !duration_config("PR_GAME_REVEAL_MS", 5000, &app.reveal_ms) ||
        !duration_config("PR_INVITE_TTL_MS", 60000, &app.invite_ttl_ms)) { pr_log_close(); return 2; }
    app.frame_timeout = 15.0;
    const char *frame_timeout = getenv("PR_FRAME_TIMEOUT_MS");
    if (frame_timeout && *frame_timeout) {
        char *end;
        long value = strtol(frame_timeout, &end, 10);
        if (*end || value < 50 || value > 60000) { fprintf(stderr, "Invalid PR_FRAME_TIMEOUT_MS\n"); pr_log_close(); return 2; }
        app.frame_timeout = (double)value / 1000.0;
    }
    for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
        app.clients[i].fd = -1;
        app.clients[i].room_index = -1;
        app.clients[i].app = &app;
    }
    app.db = pr_db_create(getenv("PR_DATABASE_URL"));
    app.hash_worker = pr_hash_worker_create();
    if (!app.db || !app.hash_worker || pr_hash_worker_fd(app.hash_worker) >= FD_SETSIZE) {
        fprintf(stderr, "auth/database initialization failed\n");
        pr_hash_worker_destroy(app.hash_worker); pr_db_destroy(app.db); return 1;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket"); pr_hash_worker_destroy(app.hash_worker); pr_db_destroy(app.db); return 1;
    }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1 || listener >= FD_SETSIZE ||
        pr_set_nonblocking(listener) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 32) < 0) {
        perror("listen setup"); close(listener);
        pr_hash_worker_destroy(app.hash_worker); pr_db_destroy(app.db); return 1;
    }
    uint64_t generation = 0;
    printf("READY %s:%ld (protocol v1, max %d connections)\n", host, port, PR_MAX_CLIENTS);
    fflush(stdout);
    int failed = 0;
    while (!stopping) {
        pr_log_flush();
        pr_game_tick_all(&app, pr_server_now());
        pr_db_tick(app.db, pr_server_now());
        pr_persist_tick(&app, pr_server_now());
        pr_social_tick(&app, pr_server_now());
        int immediate = 0;
        for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
            process_requests(&app, i);
            if (app.clients[i].requests_head && !app.clients[i].auth_busy) immediate = 1;
        }
        fd_set reads, writes;
        FD_ZERO(&reads); FD_ZERO(&writes);
        FD_SET(listener, &reads);
        int max_fd = listener;
        for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
            pr_client *c = &app.clients[i];
            if (c->fd < 0) continue;
            if (!c->read_closed) FD_SET(c->fd, &reads);
            if (c->tx.pending) FD_SET(c->fd, &writes);
            if (c->fd > max_fd) max_fd = c->fd;
        }
        int worker_fd = pr_hash_worker_fd(app.hash_worker);
        FD_SET(worker_fd, &reads);
        if (worker_fd > max_fd) max_fd = worker_fd;
        pr_db_watch(app.db, &reads, &writes, &max_fd);
        double wait = immediate ? 0 : pr_game_wait(&app, pr_server_now());
        struct timeval timeout = {0, (long)(wait * 1000000.0)};
        int ready = select(max_fd + 1, &reads, &writes, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select"); failed = 1; break;
        }
        pr_game_tick_all(&app, pr_server_now());
        /* Process existing descriptors before accept: fd_set never applies to a new fd. */
        for (size_t i = 0; i < PR_MAX_CLIENTS; ++i) {
            pr_client *c = &app.clients[i];
            if (c->fd < 0) continue;
            int fd = c->fd;
            if (FD_ISSET(fd, &reads) && read_client(c) < 0) { pr_close_client(c); continue; }
            if (FD_ISSET(fd, &writes) && pr_tx_flush(&c->tx, fd, IO_BUDGET) < 0) {
                c->close_reason = "send_error";
                pr_close_client(c);
            }
        }
        pr_db_ready(app.db, &reads, &writes, pr_server_now());
        if (FD_ISSET(worker_fd, &reads)) pr_hash_worker_dispatch(app.hash_worker);
        if (!FD_ISSET(listener, &reads)) continue;
        for (int accepted = 0; accepted < 16 && !stopping; ++accepted) {
            int fd = accept(listener, NULL, NULL);
            if (fd < 0) {
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
                break;
            }
            size_t slot = 0;
            while (slot < PR_MAX_CLIENTS && app.clients[slot].fd >= 0) ++slot;
            if (slot == PR_MAX_CLIENTS || fd >= FD_SETSIZE || pr_set_nonblocking(fd) < 0) {
                close(fd); continue;
            }
            app.clients[slot].fd = fd;
            app.clients[slot].generation = ++generation;
            pr_parser_init(&app.clients[slot].parser);
            pr_log_event("connect", generation, 0, NULL, 0, 0, 0, 0, "accepted");
        }
    }
    app.shutting_down = 1;
    for (size_t i = 0; i < PR_MAX_CLIENTS; ++i)
        if (app.clients[i].fd >= 0) {
            app.clients[i].close_reason = "server_shutdown";
            pr_close_client(&app.clients[i]);
        }
    pr_hash_worker_destroy(app.hash_worker);
    pr_db_destroy(app.db);
    pr_persist_cleanup(&app);
    pr_game_cleanup(&app);
    close(listener);
    pr_log_close();
    return failed;
}
