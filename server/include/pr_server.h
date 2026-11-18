#ifndef PR_SERVER_H
#define PR_SERVER_H
#include "pr_io.h"
#include "pr_db.h"
#include "pr_hash_worker.h"
#include "pr_room.h"
#include "pr_log.h"
#include "pr_game.h"
#include "pr_persist.h"
#include "pr_account_wire.h"

#define PR_MAX_CLIENTS 64
typedef struct pr_app pr_app;
typedef struct {
    uint64_t inviter_generation, target_generation;
    double expires;
    char code[7];
} pr_invitation;
typedef struct pr_request_node {
    struct pr_request_node *next;
    pr_header header;
    double received_at;
    uint8_t payload[];
} pr_request_node;
typedef struct pr_client {
    int fd, auth_busy, drop, room_index, read_closed;
    const char *close_reason;
    double request_started, partial_since;
    pr_app *app;
    char display_name[97];
    uint64_t generation, user_id;
    pr_parser parser;
    pr_tx_queue tx;
    pr_request_node *requests_head, *requests_tail;
    size_t request_bytes;
} pr_client;
struct pr_app {
    pr_client clients[PR_MAX_CLIENTS];
    pr_room rooms[PR_MAX_ROOMS];
    pr_match *matches[PR_MAX_ROOMS];
    pr_db *db;
    pr_hash_worker *hash_worker;
    int shutting_down;
    double frame_timeout;
    uint32_t single_ms, basket_ms, reveal_ms;
    pr_save_task *saves;
    size_t save_count;
    pr_invitation invitations[128];
    uint32_t invite_ttl_ms;
};

void pr_close_client(pr_client *);
int pr_reply(pr_client *, const pr_header *, uint16_t, const uint8_t *, size_t);
int pr_auth_handle(pr_app *, size_t, const pr_header *, const uint8_t *);
int pr_room_handle(pr_app *, size_t, const pr_header *, const uint8_t *);
void pr_room_detach(pr_client *);
void pr_room_broadcast(pr_app *, int);
int pr_game_start(pr_app *, size_t, const pr_header *);
int pr_game_guess(pr_app *, size_t, const pr_header *, const uint8_t *);
void pr_game_tick_all(pr_app *, double);
double pr_game_wait(pr_app *, double);
int pr_game_detached(pr_app *, int, uint64_t);
int pr_game_snapshot_send(pr_app *, pr_client *);
void pr_game_cleanup(pr_app *);
void pr_game_persisted(pr_app *, int room, uint64_t match_id, int success);
int pr_account_handle(pr_app *, size_t, const pr_header *, const uint8_t *);
void pr_social_detach(pr_client *);
void pr_social_tick(pr_app *, double now);
int pr_decode_credentials(const uint8_t *, size_t, char [33], char [129], size_t *);
#endif
