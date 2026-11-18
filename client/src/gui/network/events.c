#include "ui/network.h"
#include "ui/account.h"
#include "ui/app.h"
#include "ui/auth.h"
#include "ui/common.h"
#include "ui/gameplay.h"
#include "ui/results.h"
#include "ui/room.h"
#include "ui/social.h"
#include <stdio.h>
#include <string.h>

int pr_ui_dispatch_network(void *context, const pr_header *h, const uint8_t *payload, uint64_t tag) {

    pr_app *app = (pr_app *)context;
    if (h->kind == PR_RESPONSE) {
        pr_reader r;
        pr_reader_init(&r, payload, h->payload_length);
        uint16_t status = pr_get_u16(&r);
        if (!r.ok) return -1;

        if (status != PR_OK) {
            const char *vn_err = pr_status_to_vietnamese(status);
            pr_app_show_notification(app, vn_err, "error");
            if (h->code == PR_REGISTER || h->code == PR_LOGIN) {
                if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), vn_err);
                if (app->btn_login_submit) {
                    gtk_widget_set_sensitive(app->btn_login_submit, TRUE);
                    gtk_button_set_label(GTK_BUTTON(app->btn_login_submit),
                                         app->is_register_mode ? "Hoàn tất Đăng ký (Enter)" : "Đăng nhập ngay (Enter)");
                }
            } else if (h->code == PR_CREATE_ROOM || h->code == PR_JOIN_ROOM) {
                if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), vn_err);
            }
            return 0;
        }

        if (h->code == PR_PING) {
            uint32_t nonce = pr_get_u32(&r);
            char ping_msg[256];
            snprintf(ping_msg, sizeof(ping_msg), "Nhận PONG thành công từ máy chủ! Nonce: %u (Tag: %llu)",
                     nonce, (unsigned long long)tag);
            pr_app_show_notification(app, ping_msg, "info");
        } else if (h->code == PR_REGISTER) {
            if (app->btn_login_submit) {
                gtk_widget_set_sensitive(app->btn_login_submit, TRUE);
            }
            pr_app_set_auth_mode(app, 0); /* Chuyển về tab Đăng nhập */
            if (app->lbl_auth_error) {
                gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), "Đăng ký thành công! Hãy đăng nhập bằng mật khẩu của bạn.");
            }
            pr_app_show_notification(app, "Đăng ký tài khoản thành công! Vui lòng đăng nhập.", "info");
        } else if (h->code == PR_LOGIN) {
            uint64_t id = pr_get_u64(&r);
            size_t name_n = 0;
            const uint8_t *name = pr_get_string(&r, &name_n, 96);
            if (!r.ok || !id || !name || !name_n) return -1;

            app->state.is_authenticated = true;
            app->state.self_user_id = id;
            snprintf(app->state.self_username, sizeof(app->state.self_username), "%.*s", (int)name_n, (const char *)name);

            if (app->lbl_header_user_badge) {
                char ub[128];
                snprintf(ub, sizeof(ub), "%s (ID: %llu)", app->state.self_username, (unsigned long long)id);
                gtk_label_set_text(GTK_LABEL(app->lbl_header_user_badge), ub);
            }
            if (app->lbl_lobby_welcome) {
                char wb[128];
                snprintf(wb, sizeof(wb), "Chào mừng bạn trở lại, %s!", app->state.self_username);
                gtk_label_set_text(GTK_LABEL(app->lbl_lobby_welcome), wb);
            }
            if (app->btn_login_submit) {
                gtk_widget_set_sensitive(app->btn_login_submit, TRUE);
                gtk_button_set_label(GTK_BUTTON(app->btn_login_submit), "Đăng nhập ngay (Enter)");
            }
            if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), "");

            pr_app_show_screen(app, PR_SCREEN_LOBBY);
            pr_app_show_notification(app, "Đăng nhập thành công!", "info");
        } else if (h->code == PR_LOGOUT) {
            pr_ui_state_reset_session(&app->state);
            if (app->lbl_header_user_badge) {
                gtk_label_set_text(GTK_LABEL(app->lbl_header_user_badge), "Chưa đăng nhập");
            }
            pr_app_show_screen(app, PR_SCREEN_CONNECT);
            pr_app_show_notification(app, "Đã đăng xuất tài khoản.", "info");
        } else if (h->code == PR_CREATE_ROOM || h->code == PR_JOIN_ROOM) {
            pr_room_view view;
            if (!pr_room_decode(&view, payload + 2, h->payload_length - 2)) return -1;
            app->state.in_room = true;
            app->state.room = view;
            if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), "");
            pr_app_refresh_room_view(app);
            pr_app_show_screen(app, PR_SCREEN_ROOM);
            char msg[128];
            snprintf(msg, sizeof(msg), "✓ Vào phòng %s thành công!", view.code);
            pr_app_show_notification(app, msg, "info");
        } else if (h->code == PR_LEAVE_ROOM) {
            pr_ui_state_reset_room(&app->state);
            pr_app_show_screen(app, PR_SCREEN_LOBBY);
            pr_app_show_notification(app, "Đã rời khỏi phòng chờ.", "info");
        } else if (h->code == PR_SET_READY) {
            pr_app_show_notification(app, "Đã cập nhật trạng thái sẵn sàng.", "info");
        } else if (h->code == PR_START_GAME) {
            pr_app_show_notification(app, "Máy chủ đã chấp nhận bắt đầu trận đấu!", "info");
        } else if (h->code == PR_SUBMIT_GUESS) {
            app->state.has_acked_guess = true;
            app->state.acked_guess_amount = app->state.pending_guess_amount;
            for (size_t i = 0; i < app->state.game.count; ++i) {
                if (app->state.game.participants[i].user_id == app->state.self_user_id) {
                    app->state.player_submitted[i] = true;
                    app->state.player_guess_amounts[i] = app->state.acked_guess_amount;
                    break;
                }
            }
            char ack_str[128], vnd_buf[64];
            pr_format_vnd(app->state.acked_guess_amount, vnd_buf, sizeof(vnd_buf));
            snprintf(ack_str, sizeof(ack_str), "✓ Máy chủ đã ghi nhận giá: %s", vnd_buf);
            if (app->lbl_game_ack_status) {
                gtk_label_set_text(GTK_LABEL(app->lbl_game_ack_status), ack_str);
            }
            pr_app_refresh_game_view(app);
            pr_app_show_notification(app, ack_str, "info");
        } else if (h->code == PR_GET_HISTORY) {
            pr_page_view page;
            if (!pr_page_decode(&page, PR_GET_HISTORY, payload + 2, h->payload_length - 2)) return -1;
            app->state.history_count = page.count;
            app->state.history_cursor = page.next_cursor;
            app->state.history_has_next = (page.next_cursor > 0);
            for (size_t i = 0; i < page.count && i < 20; ++i) {
                app->state.history_items[i] = page.history[i];
            }
            pr_app_refresh_account_view(app);
            pr_app_show_notification(app, "✓ Đã tải lịch sử thi đấu thành công.", "info");
        } else if (h->code == PR_GET_STATS) {
            pr_stats_view stats;
            if (!pr_stats_decode(&stats, payload + 2, h->payload_length - 2)) return -1;
            app->state.stats = stats;
            app->state.stats_loaded = true;
            pr_app_refresh_account_view(app);
            pr_app_show_notification(app, "✓ Đã tải thống kê thành tích thành công.", "info");
        } else if (h->code == PR_LIST_PLAYERS) {
            pr_page_view page;
            if (!pr_page_decode(&page, PR_LIST_PLAYERS, payload + 2, h->payload_length - 2)) return -1;
            app->state.online_count = page.count;
            app->state.online_cursor = page.next_cursor;
            for (size_t i = 0; i < page.count && i < 20; ++i) {
                app->state.online_players[i] = page.players[i];
            }
            pr_app_refresh_online_players_view(app);
            pr_app_show_notification(app, "✓ Đã tải danh sách người chơi online.", "info");
        } else if (h->code == PR_INVITE_PLAYER) {
            pr_app_show_notification(app, "✓ Đã gửi lời mời tham gia phòng thi đấu!", "info");
        }
    } else if (h->kind == PR_EVENT) {
        if (h->code == PR_INVITE_RECEIVED) {
            pr_invite_view v;
            if (!pr_invite_decode(&v, payload, h->payload_length)) return -1;
            size_t slot = 0;
            double now = pr_client_now();
            while (slot < 15 && app->state.invitations[slot].active &&
                   app->state.invitations[slot].deadline > now &&
                   strcmp(app->state.invitations[slot].view.code, v.code) != 0) {
                slot++;
            }
            app->state.invitations[slot].view = v;
            app->state.invitations[slot].deadline = now + ((double)v.expires_ms / 1000.0);
            app->state.invitations[slot].active = true;
            pr_app_refresh_invitations_view(app);
            char msg[128];
            snprintf(msg, sizeof(msg), "Bạn nhận được lời mời vào phòng %s từ người chơi #%llu!",
                     v.code, (unsigned long long)v.inviter);
            pr_app_show_notification(app, msg, "info");
        } else if (h->code == PR_ROOM_UPDATE) {
            pr_room_view view;
            if (pr_room_decode(&view, payload, h->payload_length)) {
                app->state.in_room = true;
                app->state.room = view;
                pr_app_refresh_room_view(app);
                if (app->state.in_game) {
                    pr_app_refresh_game_view(app);
                }
            }
        } else if (h->code == PR_GAME_STARTED) {
            pr_game_view gv;
            app->state.in_game = true;
            if (pr_game_decode(&gv, PR_GAME_STARTED, payload, h->payload_length)) {
                app->state.game.match_id = gv.match_id;
                app->state.game.count = gv.count;
                for (size_t i = 0; i < gv.count && i < PR_MAX_PLAYERS; ++i) {
                    app->state.game.participants[i] = gv.participants[i];
                    app->state.game.totals[i] = 0;
                }
            } else if (app->state.room.player_count > 0) {
                app->state.game.count = app->state.room.player_count;
                for (size_t i = 0; i < app->state.room.player_count && i < PR_MAX_PLAYERS; ++i) {
                    app->state.game.participants[i].user_id = app->state.room.players[i].user_id;
                    g_strlcpy(app->state.game.participants[i].name, app->state.room.players[i].name, sizeof(app->state.game.participants[i].name));
                    app->state.game.participants[i].connected = 1;
                    app->state.game.totals[i] = 0;
                }
            }
            pr_app_refresh_game_view(app);
            pr_app_show_screen(app, PR_SCREEN_GAME);
            pr_app_show_notification(app, "Trận đấu đã chính thức bắt đầu!", "info");
        } else if (h->code == PR_NEW_ROUND || h->code == PR_GAME_SNAPSHOT) {
            pr_game_view gv;
            if (pr_game_decode(&gv, h->code, payload, h->payload_length)) {
                app->state.in_game = true;

                /* Bảo tồn danh sách người chơi và tổng điểm tích lũy của các vòng trước */
                uint16_t prev_count = app->state.game.count;
                pr_participant prev_parts[PR_MAX_PLAYERS];
                uint16_t prev_totals[PR_MAX_PLAYERS];
                memcpy(prev_parts, app->state.game.participants, sizeof(prev_parts));
                memcpy(prev_totals, app->state.game.totals, sizeof(prev_totals));

                app->state.game = gv;

                if (h->code == PR_NEW_ROUND) {
                    if (prev_count > 0) {
                        app->state.game.count = prev_count;
                        memcpy(app->state.game.participants, prev_parts, sizeof(prev_parts));
                        memcpy(app->state.game.totals, prev_totals, sizeof(prev_totals));
                    } else if (app->state.room.player_count > 0) {
                        app->state.game.count = app->state.room.player_count;
                        for (size_t i = 0; i < app->state.room.player_count && i < PR_MAX_PLAYERS; ++i) {
                            app->state.game.participants[i].user_id = app->state.room.players[i].user_id;
                            g_strlcpy(app->state.game.participants[i].name, app->state.room.players[i].name, sizeof(app->state.game.participants[i].name));
                            app->state.game.participants[i].connected = 1;
                            app->state.game.totals[i] = prev_totals[i];
                        }
                    }
                }

                app->state.has_acked_guess = false;
                app->state.acked_guess_amount = 0;
                app->state.pending_guess_amount = 0;
                app->state.draft_guess_str[0] = '\0';
                memset(app->state.player_submitted, 0, sizeof(app->state.player_submitted));
                memset(app->state.player_guess_amounts, 0, sizeof(app->state.player_guess_amounts));
                if (app->entry_game_guess) {
                    gtk_editable_set_text(GTK_EDITABLE(app->entry_game_guess), "");
                    gtk_widget_set_sensitive(app->entry_game_guess, TRUE);
                }
                if (app->btn_game_submit_guess) {
                    gtk_widget_set_sensitive(app->btn_game_submit_guess, TRUE);
                }
                pr_app_start_countdown(app, gv.remaining_ms);
                pr_app_refresh_game_view(app);
                pr_app_show_screen(app, PR_SCREEN_GAME);
                char r_msg[128];
                snprintf(r_msg, sizeof(r_msg), "Bắt đầu Vòng %u/5 (%s)!",
                         gv.round_no, gv.type == PR_SINGLE ? "Sản phẩm đơn" : "Giỏ hàng");
                pr_app_show_notification(app, r_msg, "info");
            }
        } else if (h->code == PR_ROUND_RESULT) {
            pr_game_view gv;
            if (pr_game_decode(&gv, PR_ROUND_RESULT, payload, h->payload_length)) {
                pr_app_stop_countdown(app);
                app->state.has_round_result = true;
                app->state.last_round_result = gv.result;
                /* Cập nhật tổng điểm tích lũy và giá đoán của các người chơi */
                if (app->state.game.count == 0 && app->state.room.player_count > 0) {
                    app->state.game.count = app->state.room.player_count;
                    for (size_t i = 0; i < app->state.room.player_count && i < PR_MAX_PLAYERS; ++i) {
                        app->state.game.participants[i].user_id = app->state.room.players[i].user_id;
                        g_strlcpy(app->state.game.participants[i].name, app->state.room.players[i].name, sizeof(app->state.game.participants[i].name));
                        app->state.game.participants[i].connected = 1;
                    }
                }
                for (size_t i = 0; i < gv.result.count; ++i) {
                    uint64_t uid = gv.result.scores[i].user_id;
                    uint16_t tot = gv.result.scores[i].total;
                    int matched = 0;
                    for (size_t j = 0; j < app->state.game.count; ++j) {
                        if (app->state.game.participants[j].user_id == uid) {
                            app->state.game.totals[j] = tot;
                            app->state.player_submitted[j] = gv.result.scores[i].submitted;
                            app->state.player_guess_amounts[j] = gv.result.scores[i].guess;
                            matched = 1;
                            break;
                        }
                    }
                    if (!matched && app->state.game.count < PR_MAX_PLAYERS) {
                        size_t idx = app->state.game.count++;
                        app->state.game.participants[idx].user_id = uid;
                        const char *pname = pr_ui_player_name(app, uid);
                        g_strlcpy(app->state.game.participants[idx].name, pname, sizeof(app->state.game.participants[idx].name));
                        app->state.game.participants[idx].connected = 1;
                        app->state.game.totals[idx] = tot;
                        app->state.player_submitted[idx] = gv.result.scores[i].submitted;
                        app->state.player_guess_amounts[idx] = gv.result.scores[i].guess;
                    }
                }
                pr_app_refresh_result_view(app);
                pr_app_refresh_game_view(app);
                pr_app_show_screen(app, PR_SCREEN_ROUND_RESULT);
                char msg[128], price_str[64];
                pr_format_vnd(gv.result.actual_total, price_str, sizeof(price_str));
                snprintf(msg, sizeof(msg), "Kết quả Vòng %u: Giá thật chính xác là %s!", gv.result.round_no, price_str);
                pr_app_show_notification(app, msg, "info");
            }
        } else if (h->code == PR_GAME_OVER) {
            pr_game_view gv;
            if (pr_game_decode(&gv, PR_GAME_OVER, payload, h->payload_length)) {
                pr_app_stop_countdown(app);
                app->state.in_game = false;
                app->state.has_final_result = true;
                app->state.final_game_over = gv;
                if (gv.state == PR_FINISHED) {
                    app->state.save_status = 1; /* PostgreSQL committed */
                    pr_app_show_notification(app, "Trận đấu kết thúc! Đã lưu kết quả an toàn vào cơ sở dữ liệu.", "info");
                } else {
                    app->state.save_status = 0;
                    const char *reason_str = (gv.abort_reason == PR_ABORT_PLAYERS)
                        ? "Có người chơi mất kết nối" : "Sự cố máy chủ";
                    char abort_msg[128];
                    snprintf(abort_msg, sizeof(abort_msg), "Trận đấu đã bị hủy bỏ (%s).", reason_str);
                    pr_app_show_notification(app, abort_msg, "error");
                }
                pr_app_refresh_result_view(app);
                pr_app_show_screen(app, PR_SCREEN_FINAL_RESULT);
            }
        } else if (h->code == PR_GAME_SAVE_FAILED) {
            pr_game_view gv;
            if (pr_game_decode(&gv, PR_GAME_SAVE_FAILED, payload, h->payload_length)) {
                app->state.save_status = 2; /* Lưu DB thất bại */
                pr_app_refresh_result_view(app);
                pr_app_show_notification(app, "Chưa nhận được xác nhận lưu; kiểm tra lịch sử khi kết nối ổn định.", "error");
            }
        } else if (h->code == PR_SUBMIT_GUESS) {
            /* Sự kiện nộp giá của người chơi (dành riêng cho khán giả theo dõi trực tiếp) */
            pr_reader r;
            pr_reader_init(&r, payload, h->payload_length);
            uint64_t sub_uid = pr_get_u64(&r);
            uint8_t sub_round = pr_get_u8(&r);
            uint64_t sub_amount = pr_get_u64(&r);
            (void)sub_round;
            if (r.ok && sub_uid > 0) {
                if (app->state.game.count == 0 && app->state.room.player_count > 0) {
                    app->state.game.count = app->state.room.player_count;
                    for (size_t i = 0; i < app->state.room.player_count && i < PR_MAX_PLAYERS; ++i) {
                        app->state.game.participants[i].user_id = app->state.room.players[i].user_id;
                        g_strlcpy(app->state.game.participants[i].name, app->state.room.players[i].name, sizeof(app->state.game.participants[i].name));
                        app->state.game.participants[i].connected = 1;
                    }
                }
                for (size_t i = 0; i < app->state.game.count; ++i) {
                    if (app->state.game.participants[i].user_id == sub_uid) {
                        app->state.player_submitted[i] = true;
                        app->state.player_guess_amounts[i] = sub_amount;
                        break;
                    }
                }
                pr_app_refresh_game_view(app);
            }
        }
    }
    return 0;
}
