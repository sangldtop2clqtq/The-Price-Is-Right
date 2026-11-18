#ifndef PR_PROTOCOL_H
#define PR_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define PR_MAGIC 0x5052u
#define PR_VERSION 1u
#define PR_HEADER_SIZE 12u
#define PR_MAX_PAYLOAD 4096u
#define PR_MAX_FRAME (PR_HEADER_SIZE + PR_MAX_PAYLOAD)

enum pr_kind { PR_REQUEST = 0, PR_RESPONSE = 1, PR_EVENT = 2 };
enum pr_code {
    PR_REGISTER = 0x0001, PR_LOGIN = 0x0002, PR_LOGOUT = 0x0003,
    PR_PING = 0x00f0,
    PR_CREATE_ROOM = 0x0101, PR_JOIN_ROOM = 0x0102,
    PR_LEAVE_ROOM = 0x0103, PR_SET_READY = 0x0104,
    PR_LIST_PLAYERS = 0x0105, PR_INVITE_PLAYER = 0x0106,
    PR_START_GAME = 0x0201, PR_SUBMIT_GUESS = 0x0202,
    PR_GET_HISTORY = 0x0301, PR_GET_STATS = 0x0302,
    PR_ROOM_UPDATE = 0x8101, PR_INVITE_RECEIVED = 0x8102,
    PR_GAME_STARTED = 0x8201, PR_NEW_ROUND = 0x8202,
    PR_ROUND_RESULT = 0x8203, PR_GAME_OVER = 0x8204,
    PR_GAME_SNAPSHOT = 0x8205, PR_GAME_SAVE_FAILED = 0x8206
};
enum pr_status {
    PR_OK = 0, PR_INVALID_MESSAGE = 1, PR_NOT_AUTHENTICATED = 2,
    PR_AUTH_FAILED = 3, PR_USER_EXISTS = 4, PR_ALREADY_ONLINE = 5,
    PR_ROOM_NOT_FOUND = 6, PR_ROOM_FULL = 7, PR_NOT_ROOM_OWNER = 8,
    PR_PLAYER_UNAVAILABLE = 9, PR_WRONG_STATE = 10,
    PR_ROUND_EXPIRED = 11, PR_ALREADY_SUBMITTED = 12,
    PR_INTERNAL_ERROR = 13, PR_NOT_IMPLEMENTED = 14
};
enum pr_role { PR_PLAYER = 0, PR_SPECTATOR = 1 };

typedef struct {
    uint8_t kind;
    uint16_t code, payload_length;
    uint32_t request_id;
} pr_header;

/* Payload pointers passed to callbacks remain valid only during the call. */
typedef int (*pr_frame_fn)(void *, const pr_header *, const uint8_t *);
typedef struct {
    uint8_t bytes[PR_MAX_FRAME];
    size_t used;
} pr_parser;

typedef struct { uint8_t *data; size_t capacity, used; int ok; } pr_writer;
typedef struct { const uint8_t *data; size_t length, used; int ok; } pr_reader;

void pr_writer_init(pr_writer *, uint8_t *, size_t);
void pr_put_u8(pr_writer *, uint8_t);
void pr_put_u16(pr_writer *, uint16_t);
void pr_put_u32(pr_writer *, uint32_t);
void pr_put_u64(pr_writer *, uint64_t);
void pr_put_string(pr_writer *, const char *, size_t);
void pr_reader_init(pr_reader *, const uint8_t *, size_t);
uint8_t pr_get_u8(pr_reader *);
uint16_t pr_get_u16(pr_reader *);
uint32_t pr_get_u32(pr_reader *);
uint64_t pr_get_u64(pr_reader *);
const uint8_t *pr_get_string(pr_reader *, size_t *, size_t);
int pr_reader_done(const pr_reader *);

int pr_decode_header(const uint8_t *, pr_header *);
/* Returns encoded length or 0 on invalid input/insufficient output capacity. */
size_t pr_encode_frame(uint8_t *, size_t, const pr_header *, const uint8_t *);
void pr_parser_init(pr_parser *);
/* Returns -1 on invalid header or callback rejection; connection must close. */
int pr_parser_feed(pr_parser *, const uint8_t *, size_t, pr_frame_fn, void *);
int pr_is_request_code(uint16_t);
/* Explicitly erase transient credentials without a platform crypto dependency. */
void pr_wipe(void *, size_t);

#endif
