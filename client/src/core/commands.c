#include "pr_client.h"
#include "pr_room_wire.h"
#include <string.h>

int pr_command_ping(pr_connection *c, uint32_t nonce) {
    uint8_t payload[4];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload)); pr_put_u32(&w, nonce);
    return pr_connection_request(c, PR_PING, payload, w.used, nonce, pr_client_now(), NULL);
}
int pr_command_auth(pr_connection *c, uint16_t code, const char *username, const char *password) {
    size_t un = strlen(username), pn = strlen(password);
    if ((code != PR_REGISTER && code != PR_LOGIN) || un < 3 || un > 32 || pn < 8 || pn > 128) return -1;
    uint8_t payload[164];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload));
    pr_put_string(&w, username, un); pr_put_string(&w, password, pn);
    int result = pr_connection_request(c, code, payload, w.used, 0, pr_client_now(), NULL);
    pr_wipe(payload, sizeof(payload));
    return result;
}
int pr_command_join(pr_connection *c, const char *code) {
    if (!pr_valid_room_code(code)) return -1;
    uint8_t payload[8];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload)); pr_put_string(&w, code, 6);
    return pr_connection_request(c, PR_JOIN_ROOM, payload, w.used, 0, pr_client_now(), NULL);
}
int pr_command_ready(pr_connection *c, uint8_t ready) {
    if (ready > 1) return -1;
    return pr_connection_request(c, PR_SET_READY, &ready, 1, 0, pr_client_now(), NULL);
}
int pr_command_empty(pr_connection *c, uint16_t code) {
    if (code != PR_LOGOUT && code != PR_CREATE_ROOM && code != PR_LEAVE_ROOM && code != PR_START_GAME && code != PR_GET_STATS) return -1;
    return pr_connection_request(c, code, NULL, 0, 0, pr_client_now(), NULL);
}
int pr_command_page(pr_connection *c, uint16_t code, uint64_t cursor, uint16_t limit) {
    if ((code != PR_GET_HISTORY && code != PR_LIST_PLAYERS) || cursor > INT64_MAX || !limit || limit > 20) return -1;
    uint8_t payload[10];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload)); pr_put_u64(&w, cursor); pr_put_u16(&w, limit);
    return pr_connection_request(c, code, payload, w.used, 0, pr_client_now(), NULL);
}
int pr_command_invite(pr_connection *c, uint64_t target) {
    if (!target || target > INT64_MAX) return -1;
    uint8_t payload[8];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload)); pr_put_u64(&w, target);
    return pr_connection_request(c, PR_INVITE_PLAYER, payload, w.used, 0, pr_client_now(), NULL);
}
int pr_command_guess(pr_connection *c, uint64_t match_id, uint8_t round_no, uint64_t amount) {
    if (!match_id || round_no < 1 || round_no > 5 || amount > UINT64_C(1000000000000)) return -1;
    uint8_t payload[17];
    pr_writer w;
    pr_writer_init(&w, payload, sizeof(payload));
    pr_put_u64(&w, match_id); pr_put_u8(&w, round_no); pr_put_u64(&w, amount);
    return pr_connection_request(c, PR_SUBMIT_GUESS, payload, w.used, amount, pr_client_now(), NULL);
}
