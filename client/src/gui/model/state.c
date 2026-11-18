#include "pr_ui_state.h"
#include <string.h>
#include <stdio.h>

void pr_ui_state_init(pr_ui_state *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->conn_status = PR_CONN_IDLE;
    snprintf(s->server_ip, sizeof(s->server_ip), "127.0.0.1");
    s->server_port = 6666;
    s->current_screen = PR_SCREEN_CONNECT;
    s->previous_screen = PR_SCREEN_CONNECT;
}

void pr_ui_state_reset_room(pr_ui_state *s) {
    if (!s) return;
    s->in_room = false;
    memset(&s->room, 0, sizeof(s->room));
    s->spectator_name_count = 0;
    memset(s->spectator_names, 0, sizeof(s->spectator_names));
}

void pr_ui_state_reset_game(pr_ui_state *s) {
    if (!s) return;
    s->in_game = false;
    memset(&s->game, 0, sizeof(s->game));
    s->has_acked_guess = false;
    s->acked_guess_amount = 0;
    s->pending_guess_request_id = 0;
    s->pending_guess_amount = 0;
    s->draft_guess_str[0] = '\0';
    s->remaining_seconds = 0;
    s->local_deadline = 0;
    memset(s->guess_history, 0, sizeof(s->guess_history));
    s->has_round_result = false;
    memset(&s->last_round_result, 0, sizeof(s->last_round_result));
    s->has_final_result = false;
    memset(&s->final_game_over, 0, sizeof(s->final_game_over));
    s->save_status = 0;
}

void pr_ui_state_reset_session(pr_ui_state *s) {
    if (!s) return;
    s->is_authenticated = false;
    s->self_user_id = 0;
    s->self_username[0] = '\0';
    pr_ui_state_reset_room(s);
    pr_ui_state_reset_game(s);
    s->history_count = 0;
    s->stats_loaded = false;
    s->online_count = 0;
    memset(s->invitations, 0, sizeof(s->invitations));
}

void pr_ui_state_init_mock(pr_ui_state *s) {
    if (!s) return;
    pr_ui_state_init(s);

    s->conn_status = PR_CONN_CONNECTED;
    s->is_authenticated = true;
    s->self_user_id = 101;
    snprintf(s->self_username, sizeof(s->self_username), "lequoctuan");

    /* Mock phòng chờ */
    s->in_room = true;
    snprintf(s->room.code, sizeof(s->room.code), "ABC123");
    s->room.state = PR_WAITING;
    s->room.host_id = 101;
    s->room.self_role = PR_PLAYER;
    s->room.player_count = 3;
    s->room.spectator_count = 1;

    s->room.players[0].user_id = 101;
    snprintf(s->room.players[0].name, sizeof(s->room.players[0].name), "lequoctuan");
    s->room.players[0].ready = 1;

    s->room.players[1].user_id = 102;
    snprintf(s->room.players[1].name, sizeof(s->room.players[1].name), "minhduc");
    s->room.players[1].ready = 1;

    s->room.players[2].user_id = 103;
    snprintf(s->room.players[2].name, sizeof(s->room.players[2].name), "hoangnam");
    s->room.players[2].ready = 0;

    /* Mock màn chơi */
    s->in_game = true;
    s->game.match_id = 1001;
    s->game.round_no = 4;
    s->game.type = PR_BASKET;
    s->game.remaining_ms = 23000;
    s->remaining_seconds = 23;
    s->game.count = 3;
    s->game.item_count = 2;

    s->game.items[0].product_id = 1;
    snprintf(s->game.items[0].name, sizeof(s->game.items[0].name), "Nước ngọt có gas giải khát");
    s->game.items[0].quantity = 2;
    snprintf(s->game.items[0].image_key, sizeof(s->game.items[0].image_key), "bev_coke_can");

    s->game.items[1].product_id = 2;
    snprintf(s->game.items[1].name, sizeof(s->game.items[1].name), "Bánh quy bơ cao cấp Danisa");
    s->game.items[1].quantity = 1;
    snprintf(s->game.items[1].image_key, sizeof(s->game.items[1].image_key), "snack_danisa_box");

    s->game.participants[0].user_id = 101;
    snprintf(s->game.participants[0].name, sizeof(s->game.participants[0].name), "lequoctuan");
    s->game.participants[0].connected = 1;
    s->game.totals[0] = 850;

    s->game.participants[1].user_id = 102;
    snprintf(s->game.participants[1].name, sizeof(s->game.participants[1].name), "minhduc");
    s->game.participants[1].connected = 1;
    s->game.totals[1] = 790;

    s->game.participants[2].user_id = 103;
    snprintf(s->game.participants[2].name, sizeof(s->game.participants[2].name), "hoangnam");
    s->game.participants[2].connected = 1;
    s->game.totals[2] = 710;

    s->has_acked_guess = true;
    s->acked_guess_amount = 400000;
    snprintf(s->draft_guess_str, sizeof(s->draft_guess_str), "450000");

    /* Mock theo dõi dự đoán cho khán giả */
    s->player_submitted[0] = true;
    s->player_guess_amounts[0] = 400000;
    s->player_submitted[1] = true;
    s->player_guess_amounts[1] = 390000;
    s->player_submitted[2] = false;
    s->player_guess_amounts[2] = 0;

    /* Mock danh sách tên khán giả: hiển thị chính tên người dùng khi đóng vai trò khán giả */
    s->spectator_name_count = 1;
    snprintf(s->spectator_names[0], sizeof(s->spectator_names[0]), "%s", s->self_username);

    /* Mock kết quả vòng */
    s->has_round_result = true;
    s->last_round_result.actual_total = 425000;
    s->last_round_result.count = 3;

    s->last_round_result.scores[0].user_id = 101;
    s->last_round_result.scores[0].guess = 400000;
    s->last_round_result.scores[0].response_ms = 4500;
    s->last_round_result.scores[0].score = 941;
    s->last_round_result.scores[0].total = 1791;
    s->last_round_result.scores[0].submitted = 1;

    s->last_round_result.scores[1].user_id = 102;
    s->last_round_result.scores[1].guess = 450000;
    s->last_round_result.scores[1].response_ms = 3200;
    s->last_round_result.scores[1].score = 941;
    s->last_round_result.scores[1].total = 1731;
    s->last_round_result.scores[1].submitted = 1;

    s->last_round_result.scores[2].user_id = 103;
    s->last_round_result.scores[2].guess = 350000;
    s->last_round_result.scores[2].response_ms = 8100;
    s->last_round_result.scores[2].score = 824;
    s->last_round_result.scores[2].total = 1534;
    s->last_round_result.scores[2].submitted = 1;

    /* Mock kết quả cuối & Lưu */
    s->has_final_result = true;
    s->final_game_over.match_id = 1001;
    s->final_game_over.state = PR_FINISHED;
    s->save_status = 1; /* Đã lưu DB */
    s->final_game_over.count = 3;
    s->final_game_over.rankings[0].user_id = 101;
    s->final_game_over.rankings[0].rank = 1;
    s->final_game_over.rankings[0].total = 4250;

    s->final_game_over.rankings[1].user_id = 102;
    s->final_game_over.rankings[1].rank = 2;
    s->final_game_over.rankings[1].total = 3810;

    s->final_game_over.rankings[2].user_id = 103;
    s->final_game_over.rankings[2].rank = 3;
    s->final_game_over.rankings[2].total = 3210;

    /* Mock lịch sử & thống kê */
    s->stats_loaded = true;
    s->stats.finished = 12;
    s->stats.wins = 5;
    s->stats.average_x100 = 365000;
    s->stats.best = 4720;

    s->history_count = 3;
    s->history_items[0].match_id = 1042;
    s->history_items[0].state = PR_FINISHED;
    s->history_items[0].rank = 1;
    s->history_items[0].total = 4250;
    s->history_items[0].started_at = 1700000000;
    s->history_items[0].ended_at = 1700000300;

    s->history_items[1].match_id = 1038;
    s->history_items[1].state = PR_FINISHED;
    s->history_items[1].rank = 2;
    s->history_items[1].total = 3810;
    s->history_items[1].started_at = 1699900000;
    s->history_items[1].ended_at = 1699900300;

    s->history_items[2].match_id = 1021;
    s->history_items[2].state = PR_ABORTED;
    s->history_items[2].rank = 0;
    s->history_items[2].total = 1200;
    s->history_items[2].started_at = 1699800000;
    s->history_items[2].ended_at = 1699800100;

    /* Mock người online */
    s->online_count = 2;
    s->online_players[0].user_id = 201;
    snprintf(s->online_players[0].name, sizeof(s->online_players[0].name), "thuylinh");
    s->online_players[1].user_id = 202;
    snprintf(s->online_players[1].name, sizeof(s->online_players[1].name), "anhquang");
}

const char *pr_status_to_vietnamese(uint16_t status) {
    switch (status) {
    case PR_OK: return "Thành công";
    case PR_INVALID_MESSAGE: return "Dữ liệu yêu cầu không hợp lệ";
    case PR_NOT_AUTHENTICATED: return "Bạn chưa đăng nhập";
    case PR_AUTH_FAILED: return "Tên đăng nhập hoặc mật khẩu không chính xác";
    case PR_USER_EXISTS: return "Tên tài khoản này đã được sử dụng";
    case PR_ALREADY_ONLINE: return "Tài khoản này đang đăng nhập ở nơi khác";
    case PR_ROOM_NOT_FOUND: return "Mã phòng không tồn tại";
    case PR_ROOM_FULL: return "Phòng đã đủ số lượng người chơi";
    case PR_NOT_ROOM_OWNER: return "Chỉ chủ phòng mới có quyền thực hiện";
    case PR_PLAYER_UNAVAILABLE: return "Người chơi không khả dụng";
    case PR_WRONG_STATE: return "Trạng thái phòng không phù hợp";
    case PR_ROUND_EXPIRED: return "Vòng chơi đã hết thời gian";
    case PR_ALREADY_SUBMITTED: return "Bạn đã nộp dự đoán cho vòng này rồi";
    case PR_INTERNAL_ERROR: return "Lỗi máy chủ nội bộ";
    case PR_NOT_IMPLEMENTED: return "Chức năng chưa được triển khai";
    default: return "Lỗi không xác định từ máy chủ";
    }
}

void pr_format_vnd(uint64_t amount, char *out, size_t out_sz) {
    if (!out || !out_sz) return;
    char raw[32];
    snprintf(raw, sizeof(raw), "%llu", (unsigned long long)amount);
    size_t len = strlen(raw);

    char formatted[64];
    size_t fi = 0;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0 && (len - i) % 3 == 0) {
            formatted[fi++] = '.';
        }
        formatted[fi++] = raw[i];
    }
    formatted[fi] = '\0';
    snprintf(out, out_sz, "%s ₫", formatted);
}


