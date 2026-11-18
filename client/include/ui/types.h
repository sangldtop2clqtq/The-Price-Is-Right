#ifndef PR_UI_TYPES_H
#define PR_UI_TYPES_H

#include <gtk/gtk.h>
#include "pr_ui_state.h"

typedef struct pr_app pr_app;

struct pr_app {
    GtkApplication *gtk_app;
    GtkWindow *window;
    GtkStack *stack;

    /* Logic State */
    pr_ui_state state;

    /* Widgets chính cho các trang */
    GtkWidget *page_connect;
    GtkWidget *page_lobby;
    GtkWidget *page_room;
    GtkWidget *page_game;
    GtkWidget *page_result;
    GtkWidget *page_account;

    /* Notification bar */
    GtkWidget *banner_bar;
    GtkWidget *lbl_banner;

    /* Widgets mạng & Header */
    GtkWidget *entry_server_ip;
    GtkWidget *entry_server_port;
    GtkWidget *btn_connect;
    GtkWidget *btn_ping;
    GtkWidget *lbl_conn_status;
    GtkWidget *lbl_header_conn_badge;
    GtkWidget *lbl_header_user_badge;

    /* Widgets Auth */
    GtkWidget *entry_username;
    GtkWidget *entry_password;
    GtkWidget *entry_password_confirm;
    GtkWidget *box_confirm_password;
    GtkWidget *btn_login_submit;
    GtkWidget *btn_tab_login;
    GtkWidget *btn_tab_register;
    GtkWidget *lbl_auth_error;
    int is_register_mode;

    /* Widgets Lobby */
    GtkWidget *lbl_lobby_welcome;
    GtkWidget *btn_create_room;
    GtkWidget *entry_room_code;
    GtkWidget *btn_join_room;
    GtkWidget *btn_logout;
    GtkWidget *lbl_lobby_error;

    /* Widgets Room (UI-04) */
    GtkWidget *lbl_room_code_badge;
    GtkWidget *btn_room_copy_code;
    GtkWidget *lbl_room_counts;
    GtkWidget *lbl_room_slots[4];
    GtkWidget *lbl_room_spectators;
    GtkWidget *btn_room_ready;
    GtkWidget *btn_room_start;
    GtkWidget *btn_room_leave;
    GtkWidget *lbl_room_hint;

    /* Widgets Gameplay (UI-05) */
    GtkWidget *lbl_game_round_badge;
    GtkWidget *lbl_game_countdown_badge;
    GtkWidget *box_game_products;
    GtkWidget *lbl_game_input_prompt;
    GtkWidget *entry_game_guess;
    GtkWidget *btn_game_submit_guess;
    GtkWidget *lbl_game_ack_status;
    GtkWidget *lbl_game_spectator_notice;
    GtkWidget *box_game_input_area;
    GtkWidget *lbl_game_scoreboard_rows[PR_MAX_PLAYERS];
    GtkWidget *lbl_game_spectator_count;
    GtkWidget *box_game_spectator_monitor;
    GtkWidget *lbl_game_spectator_guesses[PR_MAX_PLAYERS];

    /* Widget hooks cho Mốc UI-06 (Results View) */
    GtkWidget *lbl_result_title;
    GtkWidget *lbl_result_actual_price;
    GtkWidget *box_result_table;
    GtkWidget *lbl_result_sub_notice;
    GtkWidget *lbl_result_db_status;
    GtkWidget *btn_result_return_room;
    GtkWidget *btn_result_return_lobby;

    /* Widget hooks cho Mốc UI-07 (Account / History / Stats) */
    GtkWidget *lbl_account_username;
    GtkWidget *lbl_stats_finished;
    GtkWidget *lbl_stats_wins;
    GtkWidget *lbl_stats_win_rate;
    GtkWidget *lbl_stats_avg_score;
    GtkWidget *lbl_stats_best_score;
    GtkWidget *box_history_list;
    GtkWidget *lbl_history_page_info;
    GtkWidget *btn_history_prev;
    GtkWidget *btn_history_next;
    GtkWidget *btn_history_refresh;
    GtkWidget *btn_account_back;

    /* Widget hooks cho Mốc UI-08 (Online & Invitations) */
    GtkWidget *box_lobby_invites;
    GtkWidget *box_online_players;
    GtkWidget *popover_online_players;

    /* Timer IDs */
    guint net_source_id;
    guint tick_timer_id;
    guint countdown_timer_id;
};

#endif
