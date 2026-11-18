#ifndef PR_UI_STATE_H
#define PR_UI_STATE_H

#include "pr_client.h"
#include "pr_room_wire.h"
#include "pr_game_wire.h"
#include "pr_account_wire.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PR_SCREEN_CONNECT = 0,
    PR_SCREEN_AUTH,
    PR_SCREEN_LOBBY,
    PR_SCREEN_ROOM,
    PR_SCREEN_GAME,
    PR_SCREEN_ROUND_RESULT,
    PR_SCREEN_FINAL_RESULT,
    PR_SCREEN_ACCOUNT
} pr_ui_screen_t;

typedef enum {
    PR_CONN_IDLE = 0,
    PR_CONN_CONNECTING,
    PR_CONN_CONNECTED,
    PR_CONN_ERROR
} pr_ui_conn_status_t;

/* Theo dõi context của từng request guess để map chính xác ACK */
typedef struct {
    uint32_t request_id;
    uint64_t match_id;
    uint8_t  round_no;
    uint64_t amount;
    double   sent_time;
    bool     active;
} pr_tracked_guess;

typedef struct {
    pr_invite_view view;
    double deadline;
    bool active;
} pr_ui_invitation;

typedef struct {
    /* 1. Mạng & Kết nối */
    pr_connection net;
    pr_ui_conn_status_t conn_status;
    char server_ip[64];
    uint16_t server_port;
    char last_error[256];
    double error_timestamp;

    /* 2. Định danh & Phiên */
    bool is_authenticated;
    uint64_t self_user_id;
    char self_username[96];

    /* 3. Phòng chờ */
    bool in_room;
    pr_room_view room;

    /* 4. Trận đấu & Vòng chơi */
    bool in_game;
    pr_game_view game;
    double local_deadline;
    int remaining_seconds;

    /* 5. Đoán giá */
    char draft_guess_str[32];
    bool has_acked_guess;
    uint64_t acked_guess_amount;
    uint32_t pending_guess_request_id;
    uint64_t pending_guess_amount;
    pr_tracked_guess guess_history[16];

    /* Theo dõi dự đoán của từng người chơi (để Khán giả xem trực tiếp) */
    bool player_submitted[PR_MAX_PLAYERS];
    uint64_t player_guess_amounts[PR_MAX_PLAYERS];

    /* Danh sách tên khán giả theo dõi */
    char spectator_names[16][96];
    size_t spectator_name_count;

    /* 6. Kết quả */
    bool has_round_result;
    pr_result_view last_round_result;
    bool has_final_result;
    pr_game_view final_game_over;
    int save_status; /* 0: pending, 1: saved_db, 2: save_failed */

    /* 7. Tài khoản & Xã hội */
    pr_history_view history_items[20];
    size_t history_count;
    uint64_t history_cursor;
    uint32_t history_page;
    uint64_t history_cursor_stack[16];
    bool history_has_next;
    pr_stats_view stats;
    bool stats_loaded;

    pr_online_view online_players[20];
    size_t online_count;
    uint64_t online_cursor;
    pr_ui_invitation invitations[16];

    /* 8. Điều hướng */
    pr_ui_screen_t current_screen;
    pr_ui_screen_t previous_screen;
} pr_ui_state;

void pr_ui_state_init(pr_ui_state *s);
void pr_ui_state_reset_room(pr_ui_state *s);
void pr_ui_state_reset_game(pr_ui_state *s);
void pr_ui_state_reset_session(pr_ui_state *s);

/* Khởi tạo dữ liệu giả lập cho mục đích demo/mock UI ở Mốc UI-01 */
void pr_ui_state_init_mock(pr_ui_state *s);

/* Ánh xạ mã phản hồi protocol sang tiếng Việt */
const char *pr_status_to_vietnamese(uint16_t status);

/* Định dạng số tiền VND có phân cách phần nghìn */
void pr_format_vnd(uint64_t amount, char *out, size_t out_sz);

#endif


