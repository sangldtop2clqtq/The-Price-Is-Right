#include "ui/results.h"
#include "ui/app.h"
#include "ui/common.h"
#include "ui/room.h"
#include <stdio.h>
#include <string.h>

void pr_app_refresh_result_view(pr_app *app) {
    if (!app || !app->box_result_table) return;

    /* Dọn các widget con cũ trong table */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->box_result_table)) != NULL) {
        gtk_box_remove(GTK_BOX(app->box_result_table), child);
    }

    /* Kiểm tra xem đang hiển thị Kết quả chung cuộc (GAME_OVER) hay Kết quả vòng (ROUND_RESULT) */
    int show_final = (app->state.current_screen == PR_SCREEN_FINAL_RESULT) ||
                     (app->state.has_final_result && !app->state.has_round_result);

    if (show_final && app->state.has_final_result) {
        pr_game_view *gov = &app->state.final_game_over;
        if (gov->state == PR_ABORTED) {
            /* 1. Trận đấu bị hủy */
            if (app->lbl_result_title) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_title), "TRẬN ĐẤU ĐÃ BỊ HỦY BỎ");
            }
            if (app->lbl_result_actual_price) {
                const char *r_reason = (gov->abort_reason == PR_ABORT_PLAYERS)
                    ? "Lý do: Một hoặc nhiều người chơi đã mất kết nối khiến trận không thể tiếp tục."
                    : "Lý do: Máy chủ gặp sự cố hoặc quản trị viên đã hủy trận đấu.";
                gtk_label_set_text(GTK_LABEL(app->lbl_result_actual_price), r_reason);
            }
            if (app->lbl_result_sub_notice) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_sub_notice),
                    "Trận đấu bị hủy không được ghi nhận vào bảng thành tích cá nhân.");
            }
            if (app->lbl_result_db_status) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status), "Trận đấu không được lưu DB");
                gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-connected");
                gtk_widget_add_css_class(app->lbl_result_db_status, "badge-disconnected");
            }
        } else {
            /* 2. Trận đấu hoàn tất 5 vòng */
            if (app->lbl_result_title) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_title), "TỔNG KẾT TRẬN ĐẤU - 5 VÒNG HOÀN TẤT");
            }
            if (app->lbl_result_actual_price) {
                if (app->state.has_round_result) {
                    char pr_buf[64], actual_text[128];
                    pr_format_vnd(app->state.last_round_result.actual_total, pr_buf, sizeof(pr_buf));
                    snprintf(actual_text, sizeof(actual_text), "GIÁ THẬT VÒNG 5: %s", pr_buf);
                    gtk_label_set_text(GTK_LABEL(app->lbl_result_actual_price), actual_text);
                } else {
                    gtk_label_set_text(GTK_LABEL(app->lbl_result_actual_price), "Chúc mừng tất cả người chơi đã hoàn thành xuất sắc 5 vòng thi đấu!");
                }
            }

            /* Bảng xếp hạng chung cuộc */
            GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_widget_add_css_class(header_row, "table-header");

            GtkWidget *h_rank = gtk_label_new("Thứ hạng");
            gtk_widget_set_size_request(h_rank, 110, -1);
            gtk_widget_set_halign(h_rank, GTK_ALIGN_START);

            GtkWidget *h_name = gtk_label_new("Người chơi");
            gtk_widget_set_hexpand(h_name, TRUE);
            gtk_widget_set_halign(h_name, GTK_ALIGN_START);

            GtkWidget *h_total = gtk_label_new("Tổng điểm 5 vòng");
            gtk_widget_set_size_request(h_total, 160, -1);
            gtk_widget_set_halign(h_total, GTK_ALIGN_END);

            gtk_box_append(GTK_BOX(header_row), h_rank);
            gtk_box_append(GTK_BOX(header_row), h_name);
            gtk_box_append(GTK_BOX(header_row), h_total);
            gtk_box_append(GTK_BOX(app->box_result_table), header_row);

            for (size_t i = 0; i < gov->count && i < PR_MAX_PLAYERS; ++i) {
                pr_ranking *rk = &gov->rankings[i];
                if (rk->user_id == 0) continue;

                GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                gtk_widget_add_css_class(row, "table-row");

                const char *medal = (rk->rank == 1) ? "🥇 VÔ ĐỊCH" :
                                    (rk->rank == 2) ? "🥈 Á QUÂN" :
                                    (rk->rank == 3) ? "🥉 HẠNG BA" : "4 HẠNG 4";

                GtkWidget *lbl_rank = gtk_label_new(medal);
                gtk_widget_set_size_request(lbl_rank, 110, -1);
                gtk_widget_set_halign(lbl_rank, GTK_ALIGN_START);
                if (rk->rank == 1) gtk_widget_add_css_class(lbl_rank, "rank-gold");
                else if (rk->rank == 2) gtk_widget_add_css_class(lbl_rank, "rank-silver");
                else if (rk->rank == 3) gtk_widget_add_css_class(lbl_rank, "rank-bronze");

                const char *pname = pr_ui_player_name(app, rk->user_id);
                char name_buf[128];
                if (rk->user_id == app->state.self_user_id) {
                    snprintf(name_buf, sizeof(name_buf), "%s (Bạn)", pname);
                } else {
                    snprintf(name_buf, sizeof(name_buf), "%s", pname);
                }
                GtkWidget *lbl_name = gtk_label_new(name_buf);
                gtk_widget_set_hexpand(lbl_name, TRUE);
                gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);

                char total_buf[64];
                snprintf(total_buf, sizeof(total_buf), "%u điểm", rk->total);
                GtkWidget *lbl_tot = gtk_label_new(total_buf);
                gtk_widget_set_size_request(lbl_tot, 160, -1);
                gtk_widget_set_halign(lbl_tot, GTK_ALIGN_END);

                gtk_box_append(GTK_BOX(row), lbl_rank);
                gtk_box_append(GTK_BOX(row), lbl_name);
                gtk_box_append(GTK_BOX(row), lbl_tot);
                gtk_box_append(GTK_BOX(app->box_result_table), row);
            }

            if (app->lbl_result_sub_notice) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_sub_notice),
                    "Trận đấu đã khép lại. Bạn có thể quay về phòng chờ để tiếp tục trận mới cùng bạn bè!");
            }

            if (app->lbl_result_db_status) {
                gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-connected");
                gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-connecting");
                gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-disconnected");
                if (app->state.save_status == 1) {
                    gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status),
                        "Đã lưu kết quả an toàn vào cơ sở dữ liệu");
                    gtk_widget_add_css_class(app->lbl_result_db_status, "badge-connected");
                } else if (app->state.save_status == 2) {
                    gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status),
                        "Chưa nhận được xác nhận lưu; kiểm tra lịch sử khi kết nối ổn định");
                    gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-connected");
                    gtk_widget_add_css_class(app->lbl_result_db_status, "badge-disconnected");
                } else {
                    gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status),
                        "Đang ghi nhận kết quả vào cơ sở dữ liệu...");
                    gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-connected");
                    gtk_widget_add_css_class(app->lbl_result_db_status, "badge-connecting");
                }
            }
        }

        /* Nút Về phòng chờ & Về sảnh hiển thị khi trận kết thúc */
        if (app->btn_result_return_room) gtk_widget_set_visible(app->btn_result_return_room, TRUE);
        if (app->btn_result_return_lobby) gtk_widget_set_visible(app->btn_result_return_lobby, TRUE);

    } else if (app->state.has_round_result) {
        /* Hiển thị kết quả vòng */
        pr_result_view *rv = &app->state.last_round_result;

        if (app->lbl_result_title) {
            char title_buf[128];
            const char *type_str = (app->state.game.type == PR_SINGLE) ? "Sản phẩm đơn" : "Giỏ hàng";
            snprintf(title_buf, sizeof(title_buf), "KẾT QUẢ VÒNG %u/5 (%s)", rv->round_no, type_str);
            gtk_label_set_text(GTK_LABEL(app->lbl_result_title), title_buf);
        }

        if (app->lbl_result_actual_price) {
            char vnd_buf[64], actual_text[128];
            pr_format_vnd(rv->actual_total, vnd_buf, sizeof(vnd_buf));
            snprintf(actual_text, sizeof(actual_text), "GIÁ THẬT CHÍNH XÁC: %s", vnd_buf);
            gtk_label_set_text(GTK_LABEL(app->lbl_result_actual_price), actual_text);
        }

        /* Table header */
        GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(header_row, "table-header");

        GtkWidget *h_rank = gtk_label_new("Hạng");
        gtk_widget_set_size_request(h_rank, 60, -1);
        gtk_widget_set_halign(h_rank, GTK_ALIGN_START);

        GtkWidget *h_name = gtk_label_new("Người chơi");
        gtk_widget_set_hexpand(h_name, TRUE);
        gtk_widget_set_halign(h_name, GTK_ALIGN_START);

        GtkWidget *h_guess = gtk_label_new("Giá đoán");
        gtk_widget_set_size_request(h_guess, 140, -1);
        gtk_widget_set_halign(h_guess, GTK_ALIGN_END);

        GtkWidget *h_score = gtk_label_new("Điểm vòng");
        gtk_widget_set_size_request(h_score, 100, -1);
        gtk_widget_set_halign(h_score, GTK_ALIGN_END);

        GtkWidget *h_total = gtk_label_new("Tổng điểm");
        gtk_widget_set_size_request(h_total, 110, -1);
        gtk_widget_set_halign(h_total, GTK_ALIGN_END);

        gtk_box_append(GTK_BOX(header_row), h_rank);
        gtk_box_append(GTK_BOX(header_row), h_name);
        gtk_box_append(GTK_BOX(header_row), h_guess);
        gtk_box_append(GTK_BOX(header_row), h_score);
        gtk_box_append(GTK_BOX(header_row), h_total);
        gtk_box_append(GTK_BOX(app->box_result_table), header_row);

        for (size_t i = 0; i < rv->count && i < PR_MAX_PLAYERS; ++i) {
            pr_score_view *sv = &rv->scores[i];
            if (sv->user_id == 0) continue;

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_widget_add_css_class(row, "table-row");

            const char *medal = (i == 0) ? "🥇" : (i == 1) ? "🥈" : (i == 2) ? "🥉" : "4";
            GtkWidget *lbl_rank = gtk_label_new(medal);
            gtk_widget_set_size_request(lbl_rank, 60, -1);
            gtk_widget_set_halign(lbl_rank, GTK_ALIGN_START);
            if (i == 0) gtk_widget_add_css_class(lbl_rank, "rank-gold");
            else if (i == 1) gtk_widget_add_css_class(lbl_rank, "rank-silver");
            else if (i == 2) gtk_widget_add_css_class(lbl_rank, "rank-bronze");

            const char *pname = pr_ui_player_name(app, sv->user_id);
            char name_buf[128];
            if (sv->user_id == app->state.self_user_id) {
                snprintf(name_buf, sizeof(name_buf), "%s (Bạn)", pname);
            } else {
                snprintf(name_buf, sizeof(name_buf), "%s", pname);
            }
            GtkWidget *lbl_name = gtk_label_new(name_buf);
            gtk_widget_set_hexpand(lbl_name, TRUE);
            gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);

            char guess_str[64];
            if (sv->submitted) {
                char vbuf[64];
                pr_format_vnd(sv->guess, vbuf, sizeof(vbuf));
                snprintf(guess_str, sizeof(guess_str), "%s", vbuf);
            } else {
                snprintf(guess_str, sizeof(guess_str), "Chưa đoán");
            }
            GtkWidget *lbl_guess = gtk_label_new(guess_str);
            gtk_widget_set_size_request(lbl_guess, 140, -1);
            gtk_widget_set_halign(lbl_guess, GTK_ALIGN_END);

            char score_buf[64];
            snprintf(score_buf, sizeof(score_buf), "+%u đ", sv->score);
            GtkWidget *lbl_sc = gtk_label_new(score_buf);
            gtk_widget_set_size_request(lbl_sc, 100, -1);
            gtk_widget_set_halign(lbl_sc, GTK_ALIGN_END);

            char total_buf[64];
            snprintf(total_buf, sizeof(total_buf), "%u đ", sv->total);
            GtkWidget *lbl_tot = gtk_label_new(total_buf);
            gtk_widget_set_size_request(lbl_tot, 110, -1);
            gtk_widget_set_halign(lbl_tot, GTK_ALIGN_END);

            gtk_box_append(GTK_BOX(row), lbl_rank);
            gtk_box_append(GTK_BOX(row), lbl_name);
            gtk_box_append(GTK_BOX(row), lbl_guess);
            gtk_box_append(GTK_BOX(row), lbl_sc);
            gtk_box_append(GTK_BOX(row), lbl_tot);
            gtk_box_append(GTK_BOX(app->box_result_table), row);
        }

        if (rv->round_no < 5) {
            if (app->lbl_result_sub_notice) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_sub_notice),
                    "⏳ Tự động chuyển sang vòng tiếp theo trong 5 giây...");
            }
            if (app->lbl_result_db_status) {
                char db_buf[64];
                snprintf(db_buf, sizeof(db_buf), "Vòng %u/5 hoàn tất · Đang chuẩn bị vòng %u", rv->round_no, rv->round_no + 1);
                gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status), db_buf);
                gtk_widget_remove_css_class(app->lbl_result_db_status, "badge-disconnected");
                gtk_widget_add_css_class(app->lbl_result_db_status, "badge-connected");
            }
            /* Vòng 1-4 đang thi đấu nên ẩn các nút rời phòng */
            if (app->btn_result_return_room) gtk_widget_set_visible(app->btn_result_return_room, FALSE);
            if (app->btn_result_return_lobby) gtk_widget_set_visible(app->btn_result_return_lobby, FALSE);
        } else {
            if (app->lbl_result_sub_notice) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_sub_notice),
                    "⏳ Đã hoàn tất 5 vòng thi đấu! Đang đợi máy chủ tổng kết kết quả chung cuộc...");
            }
            if (app->lbl_result_db_status) {
                gtk_label_set_text(GTK_LABEL(app->lbl_result_db_status), "Đang lưu kết quả...");
            }
            if (app->btn_result_return_room) gtk_widget_set_visible(app->btn_result_return_room, TRUE);
            if (app->btn_result_return_lobby) gtk_widget_set_visible(app->btn_result_return_lobby, TRUE);
        }
    }
}

static void on_result_return_room_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_result_return_room(app);
}

static void on_result_return_lobby_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_result_return_lobby(app);
}

void pr_app_do_result_return_room(pr_app *app) {
    if (!app) return;
    if (app->state.in_room) {
        pr_app_refresh_room_view(app);
        pr_app_show_screen(app, PR_SCREEN_ROOM);
        pr_app_show_notification(app, "Đã trở về phòng chờ.", "info");
    } else {
        pr_app_show_screen(app, PR_SCREEN_LOBBY);
        pr_app_show_notification(app, "Phòng đã giải tán. Đã chuyển về sảnh chính.", "info");
    }
}

void pr_app_do_result_return_lobby(pr_app *app) {
    if (!app) return;
    if (app->state.in_room) {
        pr_app_do_room_leave(app);
    } else {
        pr_app_show_screen(app, PR_SCREEN_LOBBY);
    }
}

GtkWidget *pr_ui_build_result_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);
    gtk_widget_set_margin_top(box, 30);
    gtk_widget_set_margin_bottom(box, 30);

    /* Tiêu đề kết quả & Giá thật chính xác */
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    app->lbl_result_title = gtk_label_new("KẾT QUẢ VÒNG ĐẤU");
    gtk_widget_add_css_class(app->lbl_result_title, "app-title");

    app->lbl_result_actual_price = gtk_label_new("Đang tổng hợp dữ liệu kết quả...");
    gtk_widget_add_css_class(app->lbl_result_actual_price, "badge-countdown");

    gtk_box_append(GTK_BOX(title_box), app->lbl_result_title);
    gtk_box_append(GTK_BOX(title_box), app->lbl_result_actual_price);
    gtk_box_append(GTK_BOX(box), title_box);

    /* Bảng điểm chi tiết động */
    GtkWidget *card_table = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_table, "card");

    GtkWidget *lbl_table_header = gtk_label_new("BẢNG THÀNH TÍCH");
    gtk_widget_add_css_class(lbl_table_header, "card-title");
    gtk_box_append(GTK_BOX(card_table), lbl_table_header);

    app->box_result_table = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(card_table), app->box_result_table);
    gtk_box_append(GTK_BOX(box), card_table);

    /* Thông báo phụ */
    app->lbl_result_sub_notice = gtk_label_new("");
    gtk_widget_add_css_class(app->lbl_result_sub_notice, "text-muted");
    gtk_widget_set_halign(app->lbl_result_sub_notice, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), app->lbl_result_sub_notice);

    /* Footer: Trạng thái lưu DB và các nút điều hướng */
    GtkWidget *footer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    app->lbl_result_db_status = gtk_label_new("✓ Đã lưu kết quả trận đấu");
    gtk_widget_add_css_class(app->lbl_result_db_status, "badge-connected");
    gtk_widget_set_hexpand(app->lbl_result_db_status, TRUE);

    app->btn_result_return_room = gtk_button_new_with_label("🚪 Về phòng chờ chơi tiếp");
    gtk_widget_add_css_class(app->btn_result_return_room, "btn-primary");
    g_signal_connect(app->btn_result_return_room, "clicked", G_CALLBACK(on_result_return_room_clicked), app);

    app->btn_result_return_lobby = gtk_button_new_with_label("🏠 Về sảnh chính");
    gtk_widget_add_css_class(app->btn_result_return_lobby, "btn-secondary");
    g_signal_connect(app->btn_result_return_lobby, "clicked", G_CALLBACK(on_result_return_lobby_clicked), app);

    gtk_box_append(GTK_BOX(footer_box), app->lbl_result_db_status);
    gtk_box_append(GTK_BOX(footer_box), app->btn_result_return_room);
    gtk_box_append(GTK_BOX(footer_box), app->btn_result_return_lobby);
    gtk_box_append(GTK_BOX(box), footer_box);

    /* Render dữ liệu hiển thị lần đầu */
    pr_app_refresh_result_view(app);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    return scrolled;
}
