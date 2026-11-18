#include "pr_client.h"
#include "pr_room_wire.h"
#include "pr_game_wire.h"
#include "pr_account_wire.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    pr_connection net;
    int has_room;
    pr_room_view room;
    pr_game_view game;
    int has_game, countdown, last_seconds;
    double local_deadline;
    struct { pr_invite_view view; double deadline; } invitations[16];
} cli;
static const char *code_name(uint16_t code) {
    switch (code) {
    case PR_REGISTER: return "REGISTER";
    case PR_LOGIN: return "LOGIN";
    case PR_LOGOUT: return "LOGOUT";
    case PR_CREATE_ROOM: return "CREATE_ROOM";
    case PR_JOIN_ROOM: return "JOIN_ROOM";
    case PR_LEAVE_ROOM: return "LEAVE_ROOM";
    case PR_SET_READY: return "SET_READY";
    case PR_START_GAME: return "START_GAME";
    case PR_SUBMIT_GUESS: return "SUBMIT_GUESS";
    case PR_GET_HISTORY: return "GET_HISTORY";
    case PR_GET_STATS: return "GET_STATS";
    case PR_LIST_PLAYERS: return "LIST_PLAYERS";
    case PR_INVITE_PLAYER: return "INVITE_PLAYER";
    default: return "PING";
    }
}
static const char *status_name(uint16_t status) {
    const char *names[] = {"OK", "INVALID_MESSAGE", "NOT_AUTHENTICATED", "AUTH_FAILED",
        "USER_EXISTS", "ALREADY_ONLINE", "ROOM_NOT_FOUND", "ROOM_FULL", "NOT_ROOM_OWNER",
        "PLAYER_UNAVAILABLE", "WRONG_STATE", "ROUND_EXPIRED", "ALREADY_SUBMITTED",
        "INTERNAL_ERROR", "NOT_IMPLEMENTED"};
    return status <= PR_NOT_IMPLEMENTED ? names[status] : "UNKNOWN_STATUS";
}
static int show_room(cli *c, const uint8_t *payload, size_t n, const char *label) {
    pr_room_view view;
    if (!pr_room_decode(&view, payload, n)) return -1;
    c->room = view; c->has_room = 1;
    printf("%s code=%s state=%s host=%llu revision=%u role=%s spectators=%u\n", label,
        view.code, view.state == PR_WAITING ? "WAITING" : "IN_GAME",
        (unsigned long long)view.host_id, view.revision,
        view.self_role == PR_PLAYER ? "PLAYER" : "SPECTATOR", view.spectator_count);
    for (size_t i = 0; i < view.player_count; ++i) {
        const pr_player_view *p = &view.players[i];
        printf("  PLAYER user_id=%llu name=%s ready=%u%s\n", (unsigned long long)p->user_id,
               p->name, p->ready, p->user_id == view.host_id ? " HOST" : "");
    }
    fflush(stdout);
    return 0;
}
static int show_game(cli *c, const pr_header *h, const uint8_t *payload) {
    if (h->code == PR_SUBMIT_GUESS) return 0;
    pr_game_view view;
    if (!pr_game_decode(&view, h->code, payload, h->payload_length)) return -1;
    c->has_game = 1;
    if (h->code == PR_GAME_SAVE_FAILED) {
        c->has_game = 0;
        printf("GAME_SAVE_FAILED match_id=%llu status=%u results NOT saved; room returns to WAITING\n",
               (unsigned long long)view.match_id, view.save_status);
    } else if (h->code == PR_GAME_STARTED) {
        c->game = view;
        printf("GAME_STARTED match_id=%llu players=%u rounds=5\n", (unsigned long long)view.match_id, view.count);
    } else if (h->code == PR_NEW_ROUND || h->code == PR_GAME_SNAPSHOT) {
        if (h->code == PR_NEW_ROUND) {
            view.count = c->game.count;
            memcpy(view.participants, c->game.participants, sizeof(view.participants));
            memcpy(view.totals, c->game.totals, sizeof(view.totals));
            view.state = PR_RUNNING;
        }
        c->game = view; c->last_seconds = -1;
        c->local_deadline = pr_client_now() + (double)view.remaining_ms / 1000.0;
        printf("%s match_id=%llu round=%u type=%s remaining_ms=%u duration_ms=%u%s\n",
            h->code == PR_NEW_ROUND ? "NEW_ROUND" : "GAME_SNAPSHOT", (unsigned long long)view.match_id,
            view.round_no, view.type == PR_SINGLE ? "SINGLE" : "BASKET", view.remaining_ms, view.duration_ms,
            c->room.self_role == PR_SPECTATOR ? " WATCHING" : "");
        for (size_t i = 0; i < view.item_count; ++i)
            printf("  ITEM id=%llu name=%s quantity=%u image_key=%s\n", (unsigned long long)view.items[i].product_id,
                   view.items[i].name, view.items[i].quantity, view.items[i].image_key);
        if (view.has_result) printf("  REVEALED actual_total=%llu\n", (unsigned long long)view.result.actual_total);
    } else if (h->code == PR_ROUND_RESULT) {
        c->game.round_state = PR_REVEALED; c->game.result = view.result;
        printf("ROUND_RESULT match_id=%llu round=%u actual_total=%llu\n", (unsigned long long)view.match_id,
               view.round_no, (unsigned long long)view.result.actual_total);
        for (size_t i = 0; i < view.result.count; ++i) {
            pr_score_view *score = &view.result.scores[i];
            c->game.totals[i] = score->total;
            if (score->submitted) printf("  SCORE user_id=%llu guess=%llu response_ms=%u score=%u total=%u\n",
                (unsigned long long)score->user_id, (unsigned long long)score->guess, score->response_ms, score->score, score->total);
            else printf("  SCORE user_id=%llu NOT_SUBMITTED score=0 total=%u\n", (unsigned long long)score->user_id, score->total);
        }
    } else if (h->code == PR_GAME_OVER) {
        c->game = view;
        printf("GAME_OVER match_id=%llu outcome=%s abort_reason=%u (saved in PostgreSQL)\n",
               (unsigned long long)view.match_id, view.state == PR_FINISHED ? "FINISHED" : "ABORTED", view.abort_reason);
        for (size_t i = 0; i < view.count; ++i)
            printf("  RANK user_id=%llu rank=%u total=%u\n", (unsigned long long)view.rankings[i].user_id,
                   view.rankings[i].rank, view.rankings[i].total);
    }
    fflush(stdout);
    return 0;
}
static int on_response(void *context, const pr_header *h, const uint8_t *payload, uint64_t tag) {
    cli *c = context;
    if (h->kind == PR_EVENT) {
        if (h->request_id) return -1;
        if (h->code == PR_ROOM_UPDATE) return show_room(c, payload, h->payload_length, "ROOM_UPDATE");
        if (h->code == PR_INVITE_RECEIVED) {
            pr_invite_view v;
            if (!pr_invite_decode(&v, payload, h->payload_length)) return -1;
            size_t slot = 0;
            while (slot < 15 && c->invitations[slot].deadline > pr_client_now() &&
                   strcmp(c->invitations[slot].view.code, v.code)) ++slot;
            c->invitations[slot].view = v;
            c->invitations[slot].deadline = pr_client_now() + (double)v.expires_ms / 1000.0;
            printf("INVITE_RECEIVED inviter_id=%llu room=%s expires_ms=%u; use: accept %s\n",
                   (unsigned long long)v.inviter, v.code, v.expires_ms, v.code);
            fflush(stdout); return 0;
        }
        return show_game(c, h, payload);
    }
    if (h->kind != PR_RESPONSE) return -1;
    pr_reader r;
    pr_reader_init(&r, payload, h->payload_length);
    uint16_t status = pr_get_u16(&r);
    if (!r.ok || status > PR_NOT_IMPLEMENTED) return -1;
    if (status != PR_OK) {
        if (!pr_reader_done(&r)) return -1;
        printf("ERROR %s request_id=%u status=%s(%u)\n",
               code_name(h->code), h->request_id, status_name(status), status);
    } else if (h->code == PR_PING) {
        uint32_t nonce = pr_get_u32(&r);
        if (!pr_reader_done(&r) || nonce != (uint32_t)tag) return -1;
        printf("PONG request_id=%u nonce=%u\n", h->request_id, nonce);
    } else if (h->code == PR_REGISTER || h->code == PR_LOGIN) {
        uint64_t id = pr_get_u64(&r);
        size_t name_n = 0;
        const uint8_t *name = NULL;
        if (h->code == PR_LOGIN) name = pr_get_string(&r, &name_n, 96);
        if (!pr_reader_done(&r) || !id || (h->code == PR_LOGIN && (!name || !name_n))) return -1;
        printf("%s OK request_id=%u user_id=%llu", code_name(h->code), h->request_id, (unsigned long long)id);
        if (name) printf(" name=%.*s", (int)name_n, (const char *)name);
        putchar('\n');
    } else if (h->code == PR_GET_HISTORY || h->code == PR_LIST_PLAYERS) {
        pr_page_view page;
        if (!pr_page_decode(&page, h->code, payload + 2, h->payload_length - 2)) return -1;
        printf("%s count=%u next_cursor=%llu\n", h->code == PR_GET_HISTORY ? "HISTORY" : "PLAYERS",
               page.count, (unsigned long long)page.next_cursor);
        for (size_t i = 0; i < page.count; ++i) {
            if (h->code == PR_LIST_PLAYERS) printf("  ONLINE user_id=%llu name=%s\n",
                (unsigned long long)page.players[i].user_id, page.players[i].name);
            else {
                pr_history_view *entry = &page.history[i];
                printf("  MATCH id=%llu outcome=%s total=%u rank=%u started_ms=%llu ended_ms=%llu\n",
                    (unsigned long long)entry->match_id, entry->state == PR_FINISHED ? "FINISHED" : "ABORTED",
                    entry->total, entry->rank, (unsigned long long)entry->started_at, (unsigned long long)entry->ended_at);
            }
        }
    } else if (h->code == PR_GET_STATS) {
        pr_stats_view stats;
        if (!pr_stats_decode(&stats, payload + 2, h->payload_length - 2)) return -1;
        printf("STATS finished=%u wins=%u win_rate=%.2f%% average=%.2f best=%u\n", stats.finished,
            stats.wins, stats.finished ? 100.0 * stats.wins / stats.finished : 0.0,
            (double)stats.average_x100 / 100.0, stats.best);
    } else if (h->code == PR_START_GAME) {
        uint64_t id = pr_get_u64(&r);
        if (!id || !pr_reader_done(&r)) return -1;
        memset(&c->game, 0, sizeof(c->game)); c->game.match_id = id; c->has_game = 1;
        printf("START_GAME OK request_id=%u match_id=%llu PREPARING\n", h->request_id, (unsigned long long)id);
    } else if (h->code == PR_SUBMIT_GUESS) {
        if (pr_get_u8(&r) != 1 || !pr_reader_done(&r)) return -1;
        printf("SUBMIT_GUESS OK request_id=%u accepted=1 guess=%llu\n", h->request_id, (unsigned long long)tag);
    } else if (h->code == PR_CREATE_ROOM || h->code == PR_JOIN_ROOM) {
        if (show_room(c, payload + 2, h->payload_length - 2, code_name(h->code)) < 0) return -1;
    } else {
        if (!pr_reader_done(&r)) return -1;
        if (h->code == PR_LEAVE_ROOM || h->code == PR_LOGOUT) {
            c->has_room = 0; c->has_game = 0; memset(&c->room, 0, sizeof(c->room));
            if (h->code == PR_LOGOUT) memset(c->invitations, 0, sizeof(c->invitations));
        }
        printf("%s OK request_id=%u\n", code_name(h->code), h->request_id);
    }
    fflush(stdout);
    return 0;
}
static volatile sig_atomic_t interrupted;
static void stop_cli(int signo) { (void)signo; interrupted = 1; }
static long parse_number(const char *s, long minimum, long maximum) {
    char *end;
    errno = 0;
    long value = strtol(s, &end, 10);
    return errno || !*s || *end || value < minimum || value > maximum ? -1 : value;
}
static int page_args(const char *text, uint64_t *cursor, uint16_t *limit) {
    while (*text == ' ') ++text;
    *cursor = 0; *limit = 10;
    if (!*text) return 1;
    if (*text < '0' || *text > '9') return 0;
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || value > INT64_MAX) return 0;
    *cursor = value;
    while (*end == ' ') ++end;
    if (!*end) return 1;
    if (*end < '0' || *end > '9') return 0;
    errno = 0;
    unsigned long count = strtoul(end, &end, 10);
    while (*end == ' ') ++end;
    if (errno || *end || count < 1 || count > 20) return 0;
    *limit = (uint16_t)count; return 1;
}
int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s IPv4-address port [count:1..64|--interactive]\n", argv[0]);
        return 2;
    }
    long port = parse_number(argv[2], 1, 65535);
    int interactive = argc == 4 && strcmp(argv[3], "--interactive") == 0;
    long count = argc == 4 && !interactive ? parse_number(argv[3], 1, 64) : 1;
    if (port < 0 || count < 0) return 2;
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_cli;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    cli c;
    memset(&c, 0, sizeof(c));
    c.countdown = interactive && isatty(STDIN_FILENO);
    pr_connection_init(&c.net, on_response, &c);
    if (pr_connection_open(&c.net, argv[1], (uint16_t)port, pr_client_now()) < 0) {
        fprintf(stderr, "%s\n", c.net.error); return 1;
    }
    int result = 0, input_open = interactive;
    char line[512], auth_username[33] = {0};
    uint16_t password_code = 0;
    struct termios saved_terminal;
    int echo_disabled = 0;
    if (!interactive) {
        for (long i = 0; i < count; ++i)
            if (pr_command_ping(&c.net, (uint32_t)(i + 1)) < 0) { result = 1; goto done; }
    } else puts("Commands: ping <nonce>, register <username>, login <username>, logout, create, join <CODE>, leave, ready <0|1>, room, start, game, guess <VND>, history [cursor] [limit], stats, players [cursor] [limit], invite <id>, invitations, accept <CODE>, quit");
    size_t line_length = 0;
    while (!interrupted && (input_open || c.net.outstanding)) {
        fd_set reads, writes;
        FD_ZERO(&reads); FD_ZERO(&writes);
        int max_fd = input_open ? STDIN_FILENO : -1;
        if (input_open) FD_SET(STDIN_FILENO, &reads);
        pr_connection_watch(&c.net, &reads, &writes, &max_fd);
        struct timeval timeout = {0, 100000};
        int ready = select(max_fd + 1, &reads, &writes, NULL, &timeout);
        if (ready < 0) { if (errno == EINTR) continue; perror("select"); result = 1; break; }
        if (pr_connection_process(&c.net, &reads, &writes, pr_client_now()) < 0) {
            fprintf(stderr, "%s\n", c.net.error); result = 1; break;
        }
        if (c.countdown && c.has_game && c.game.state == PR_RUNNING && c.game.round_state == PR_ACCEPTING) {
            double remaining = c.local_deadline - pr_client_now();
            int seconds = remaining <= 0 ? 0 : (int)remaining + 1;
            if (seconds != c.last_seconds) {
                printf("COUNTDOWN round=%u seconds=%d\n", c.game.round_no, seconds);
                fflush(stdout); c.last_seconds = seconds;
            }
        }
        if (input_open && FD_ISSET(STDIN_FILENO, &reads)) {
            /* Read one byte to avoid stdio read-ahead hiding piped commands from select. */
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (!n) input_open = 0;
            else if (n < 0 && errno != EINTR) { result = 1; break; }
            else if (n > 0 && ch == '\n') {
                line[line_length] = '\0';
                if (password_code) {
                    if (echo_disabled) { tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal); echo_disabled = 0; putchar('\n'); }
                    if (pr_command_auth(&c.net, password_code, auth_username, line) < 0)
                        fprintf(stderr, "username/password length invalid or request queue full\n");
                    password_code = 0;
                    pr_wipe(auth_username, sizeof(auth_username));
                } else if (!strcmp(line, "quit")) input_open = 0;
                else if (!strcmp(line, "stats")) {
                    if (pr_command_empty(&c.net, PR_GET_STATS) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strcmp(line, "history") || !strncmp(line, "history ", 8) ||
                           !strcmp(line, "players") || !strncmp(line, "players ", 8)) {
                    uint64_t cursor; uint16_t limit;
                    uint16_t code = line[0] == 'h' ? PR_GET_HISTORY : PR_LIST_PLAYERS;
                    if (!page_args(line + 7, &cursor, &limit)) fprintf(stderr, "use history/players [cursor] [limit:1..20]\n");
                    else if (pr_command_page(&c.net, code, cursor, limit) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strncmp(line, "invite ", 7)) {
                    long id = parse_number(line + 7, 1, INT64_MAX);
                    if (id < 0 || pr_command_invite(&c.net, (uint64_t)id) < 0) fprintf(stderr, "invalid user ID or request queue full\n");
                } else if (!strcmp(line, "invitations")) {
                    for (size_t i = 0; i < 16; ++i) if (c.invitations[i].deadline > pr_client_now())
                        printf("INVITATION room=%s inviter_id=%llu remaining_ms=%.0f\n", c.invitations[i].view.code,
                            (unsigned long long)c.invitations[i].view.inviter,
                            (c.invitations[i].deadline - pr_client_now()) * 1000.0);
                } else if (!strncmp(line, "accept ", 7)) {
                    int found = 0;
                    for (size_t i = 0; i < 16; ++i) if (c.invitations[i].deadline > pr_client_now() &&
                        !strcmp(c.invitations[i].view.code, line + 7)) found = 1;
                    if (!found) fprintf(stderr, "invitation missing or expired; join by room code remains available\n");
                    else if (pr_command_join(&c.net, line + 7) < 0) fprintf(stderr, "invalid code or request queue full\n");
                } else if (!strcmp(line, "game")) {
                    if (!c.has_game) puts("No match yet");
                    else printf("GAME match_id=%llu state=%u round=%u phase=%u role=%s\n",
                        (unsigned long long)c.game.match_id, c.game.state, c.game.round_no, c.game.round_state,
                        c.room.self_role == PR_SPECTATOR ? "SPECTATOR" : "PLAYER");
                } else if (!strncmp(line, "guess ", 6)) {
                    long amount = parse_number(line + 6, 0, 1000000000000L);
                    if (amount < 0) fprintf(stderr, "guess must be 0..1000000000000 VND\n");
                    else if (!c.has_game || !c.has_room || c.room.self_role != PR_PLAYER ||
                             c.game.state != PR_RUNNING || c.game.round_state != PR_ACCEPTING)
                        fprintf(stderr, "only an active PLAYER can guess during ACCEPTING\n");
                    else if (pr_command_guess(&c.net, c.game.match_id, c.game.round_no, (uint64_t)amount) < 0)
                        fprintf(stderr, "request queue full\n");
                } else if (!strcmp(line, "room")) {
                    if (!c.has_room) puts("Not in a room");
                    else printf("ROOM code=%s role=%s host=%llu players=%u spectators=%u\n", c.room.code,
                        c.room.self_role == PR_PLAYER ? "PLAYER" : "SPECTATOR",
                        (unsigned long long)c.room.host_id, c.room.player_count, c.room.spectator_count);
                } else if (!strcmp(line, "create") || !strcmp(line, "leave") || !strcmp(line, "start")) {
                    uint16_t code = line[0] == 'c' ? PR_CREATE_ROOM : line[0] == 'l' ? PR_LEAVE_ROOM : PR_START_GAME;
                    if (pr_command_empty(&c.net, code) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strncmp(line, "join ", 5)) {
                    const char *code = line + 5;
                    if (!pr_valid_room_code(code)) fprintf(stderr, "room code must be 6 uppercase letters/digits\n");
                    else if (pr_command_join(&c.net, code) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strcmp(line, "ready 0") || !strcmp(line, "ready 1")) {
                    uint8_t ready = (uint8_t)(line[6] - '0');
                    if (pr_command_ready(&c.net, ready) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strcmp(line, "logout")) {
                    if (pr_command_empty(&c.net, PR_LOGOUT) < 0) fprintf(stderr, "request queue full\n");
                } else if (!strncmp(line, "register ", 9) || !strncmp(line, "login ", 6)) {
                    uint16_t code = line[0] == 'r' ? PR_REGISTER : PR_LOGIN;
                    char *username = line + (code == PR_REGISTER ? 9 : 6);
                    char *password = strchr(username, ' ');
                    if (password) {
                        *password++ = 0;
                        if (pr_command_auth(&c.net, code, username, password) < 0)
                            fprintf(stderr, "username/password length invalid or request queue full\n");
                    } else if (strlen(username) >= 3 && strlen(username) <= 32) {
                        strcpy(auth_username, username);
                        password_code = code;
                        if (isatty(STDIN_FILENO)) {
                            if (tcgetattr(STDIN_FILENO, &saved_terminal) != 0) { result = 1; break; }
                            struct termios hidden = saved_terminal;
                            hidden.c_lflag &= (tcflag_t)~ECHO;
                            if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) != 0) { result = 1; break; }
                            echo_disabled = 1;
                            printf("Password: "); fflush(stdout);
                        }
                    } else fprintf(stderr, "username must contain 3..32 bytes\n");
                } else if (!strncmp(line, "ping ", 5)) {
                    long nonce = parse_number(line + 5, 0, 4294967295L);
                    if (nonce < 0 || pr_command_ping(&c.net, (uint32_t)nonce) < 0)
                        fprintf(stderr, "invalid nonce or 64 requests already pending\n");
                } else if (line_length) fprintf(stderr, "use ping, register, login, logout, create, join, leave, ready, room, start or quit\n");
                pr_wipe(line, sizeof(line));
                line_length = 0;
            } else if (n > 0) {
                if (line_length + 1 >= sizeof(line)) { fprintf(stderr, "command too long\n"); result = 1; break; }
                line[line_length++] = ch;
            }
        }
    }
done:
    if (echo_disabled) tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
    pr_wipe(line, sizeof(line));
    pr_wipe(auth_username, sizeof(auth_username));
    if (interrupted) result = 130;
    pr_connection_close(&c.net);
    return result;
}
