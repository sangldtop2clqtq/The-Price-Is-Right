#include "ui/account.h"
#include "ui/app.h"
#include "ui/network.h"
#include <stdio.h>
#include <string.h>

void pr_app_do_get_stats(pr_app *app) {
    if (!app || app->state.net.fd < 0 || !app->state.is_authenticated) return;
    if (pr_command_empty(&app->state.net, PR_GET_STATS) == 0) {
        pr_net_source_notify_send(app);
    }
}

void pr_app_do_get_history(pr_app *app, uint64_t cursor) {
    if (!app || app->state.net.fd < 0 || !app->state.is_authenticated) return;
    if (pr_command_page(&app->state.net, PR_GET_HISTORY, cursor, 10) == 0) {
        pr_net_source_notify_send(app);
    }
}

void pr_app_open_account(pr_app *app) {
    if (!app) return;
    app->state.history_page = 1;
    app->state.history_cursor = 0;
    app->state.history_cursor_stack[0] = 0;
    pr_app_show_screen(app, PR_SCREEN_ACCOUNT);
    pr_app_do_get_stats(app);
    pr_app_do_get_history(app, 0);
    pr_app_refresh_account_view(app);
}

void pr_app_do_history_prev(pr_app *app) {
    if (!app || app->state.history_page <= 1) return;
    app->state.history_page--;
    uint64_t prev_cursor = app->state.history_cursor_stack[app->state.history_page - 1];
    pr_app_do_get_history(app, prev_cursor);
}

void pr_app_do_history_next(pr_app *app) {
    if (!app || !app->state.history_has_next) return;
    if (app->state.history_page < 15) {
        app->state.history_cursor_stack[app->state.history_page] = app->state.history_cursor;
        app->state.history_page++;
        pr_app_do_get_history(app, app->state.history_cursor);
    }
}

static void on_history_prev_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_history_prev(app);
}

static void on_history_next_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_history_next(app);
}

static void on_history_refresh_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    uint64_t cur = (app->state.history_page > 0) ? app->state.history_cursor_stack[app->state.history_page - 1] : 0;
    pr_app_do_get_stats(app);
    pr_app_do_get_history(app, cur);
}

static void on_account_back_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    if (app->state.in_room) {
        pr_app_show_screen(app, PR_SCREEN_ROOM);
    } else {
        pr_app_show_screen(app, PR_SCREEN_LOBBY);
    }
}

void pr_app_refresh_account_view(pr_app *app) {
    if (!app) return;

    /* 1. Header username */
    if (app->lbl_account_username) {
        char un_buf[128];
        snprintf(un_buf, sizeof(un_buf), "TÀI KHOẢN & LỊCH SỬ ĐẤU: %s",
                 app->state.self_username[0] ? app->state.self_username : "Khách");
        gtk_label_set_text(GTK_LABEL(app->lbl_account_username), un_buf);
    }

    /* 2. Thống kê */
    if (app->state.stats_loaded) {
        if (app->lbl_stats_finished) {
            char b[64];
            snprintf(b, sizeof(b), "Trận hoàn thành: %u", app->state.stats.finished);
            gtk_label_set_text(GTK_LABEL(app->lbl_stats_finished), b);
        }
        if (app->lbl_stats_wins) {
            char b[64];
            snprintf(b, sizeof(b), "Hạng nhất: %u", app->state.stats.wins);
            gtk_label_set_text(GTK_LABEL(app->lbl_stats_wins), b);
        }
        if (app->lbl_stats_win_rate) {
            char b[64];
            double wr = app->state.stats.finished ? (100.0 * (double)app->state.stats.wins / (double)app->state.stats.finished) : 0.0;
            snprintf(b, sizeof(b), "Tỷ lệ thắng: %.1f%%", wr);
            gtk_label_set_text(GTK_LABEL(app->lbl_stats_win_rate), b);
        }
        if (app->lbl_stats_avg_score) {
            char b[64];
            double avg = (double)app->state.stats.average_x100 / 100.0;
            snprintf(b, sizeof(b), "Điểm trung bình: %.1f đ", avg);
            gtk_label_set_text(GTK_LABEL(app->lbl_stats_avg_score), b);
        }
        if (app->lbl_stats_best_score) {
            char b[64];
            snprintf(b, sizeof(b), "Điểm kỷ lục: %u đ", app->state.stats.best);
            gtk_label_set_text(GTK_LABEL(app->lbl_stats_best_score), b);
        }
    }

    /* 3. Lịch sử danh sách trận */
    if (app->box_history_list) {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(app->box_history_list)) != NULL) {
            gtk_box_remove(GTK_BOX(app->box_history_list), child);
        }

        if (app->state.history_count == 0) {
            GtkWidget *empty_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(empty_row, "table-row");
            GtkWidget *lbl_empty = gtk_label_new("Chưa có trận đấu nào được lưu trong lịch sử.");
            gtk_widget_add_css_class(lbl_empty, "text-muted");
            gtk_box_append(GTK_BOX(empty_row), lbl_empty);
            gtk_box_append(GTK_BOX(app->box_history_list), empty_row);
        } else {
            for (size_t i = 0; i < app->state.history_count && i < 20; ++i) {
                pr_history_view *h = &app->state.history_items[i];
                if (h->match_id == 0) continue;

                GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                gtk_widget_add_css_class(row, "table-row");

                /* Mã trận */
                char id_str[64];
                snprintf(id_str, sizeof(id_str), "Trận #%llu", (unsigned long long)h->match_id);
                GtkWidget *lbl_id = gtk_label_new(id_str);
                gtk_widget_set_size_request(lbl_id, 110, -1);
                gtk_widget_set_halign(lbl_id, GTK_ALIGN_START);

                /* Trạng thái outcome */
                const char *st_text = (h->state == PR_FINISHED) ? "FINISHED" : "ABORTED";
                GtkWidget *lbl_st = gtk_label_new(st_text);
                gtk_widget_set_size_request(lbl_st, 90, -1);
                gtk_widget_set_halign(lbl_st, GTK_ALIGN_START);
                if (h->state == PR_FINISHED) gtk_widget_add_css_class(lbl_st, "text-success");
                else gtk_widget_add_css_class(lbl_st, "text-danger");

                /* Thứ hạng */
                char rank_str[64];
                if (h->state == PR_FINISHED) {
                    if (h->rank == 1) snprintf(rank_str, sizeof(rank_str), "Hạng 1 (Vô địch)");
                    else if (h->rank == 2) snprintf(rank_str, sizeof(rank_str), "Hạng 2");
                    else if (h->rank == 3) snprintf(rank_str, sizeof(rank_str), "Hạng 3");
                    else snprintf(rank_str, sizeof(rank_str), "Hạng %u", h->rank);
                } else {
                    snprintf(rank_str, sizeof(rank_str), "Hủy trận");
                }
                GtkWidget *lbl_rk = gtk_label_new(rank_str);
                gtk_widget_set_hexpand(lbl_rk, TRUE);
                gtk_widget_set_halign(lbl_rk, GTK_ALIGN_START);

                /* Tổng điểm */
                char tot_str[64];
                snprintf(tot_str, sizeof(tot_str), "%u điểm", h->total);
                GtkWidget *lbl_tot = gtk_label_new(tot_str);
                gtk_widget_set_size_request(lbl_tot, 120, -1);
                gtk_widget_set_halign(lbl_tot, GTK_ALIGN_END);

                gtk_box_append(GTK_BOX(row), lbl_id);
                gtk_box_append(GTK_BOX(row), lbl_st);
                gtk_box_append(GTK_BOX(row), lbl_rk);
                gtk_box_append(GTK_BOX(row), lbl_tot);
                gtk_box_append(GTK_BOX(app->box_history_list), row);
            }
        }
    }

    /* 4. Điều khiển phân trang */
    if (app->lbl_history_page_info) {
        char p_buf[64];
        uint32_t pg = app->state.history_page ? app->state.history_page : 1;
        snprintf(p_buf, sizeof(p_buf), "Trang %u", pg);
        gtk_label_set_text(GTK_LABEL(app->lbl_history_page_info), p_buf);
    }
    if (app->btn_history_prev) {
        gtk_widget_set_sensitive(app->btn_history_prev, app->state.history_page > 1);
    }
    if (app->btn_history_next) {
        gtk_widget_set_sensitive(app->btn_history_next, app->state.history_has_next);
    }
}

GtkWidget *pr_ui_build_account_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    /* Tiêu đề & nút quay lại */
    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    app->lbl_account_username = gtk_label_new("TÀI KHOẢN & LỊCH SỬ ĐẤU");
    gtk_widget_add_css_class(app->lbl_account_username, "app-title");
    gtk_widget_set_halign(app->lbl_account_username, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_row), app->lbl_account_username);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);

    app->btn_account_back = gtk_button_new_with_label("Quay lại sảnh");
    gtk_widget_add_css_class(app->btn_account_back, "btn-secondary");
    g_signal_connect(app->btn_account_back, "clicked", G_CALLBACK(on_account_back_clicked), app);
    gtk_box_append(GTK_BOX(top_row), app->btn_account_back);
    gtk_box_append(GTK_BOX(box), top_row);

    /* Thẻ thống kê */
    GtkWidget *card_stats = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_add_css_class(card_stats, "card");

    app->lbl_stats_finished = gtk_label_new("Trận hoàn thành: 0");
    app->lbl_stats_wins = gtk_label_new("Hạng nhất: 0");
    app->lbl_stats_win_rate = gtk_label_new("Tỷ lệ thắng: 0.0%");
    app->lbl_stats_avg_score = gtk_label_new("Điểm trung bình: 0.0 đ");
    app->lbl_stats_best_score = gtk_label_new("Điểm kỷ lục: 0 đ");

    gtk_box_append(GTK_BOX(card_stats), app->lbl_stats_finished);
    gtk_box_append(GTK_BOX(card_stats), app->lbl_stats_wins);
    gtk_box_append(GTK_BOX(card_stats), app->lbl_stats_win_rate);
    gtk_box_append(GTK_BOX(card_stats), app->lbl_stats_avg_score);
    gtk_box_append(GTK_BOX(card_stats), app->lbl_stats_best_score);
    gtk_box_append(GTK_BOX(box), card_stats);

    /* Thẻ lịch sử trận đấu */
    GtkWidget *card_history = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_history, "card");

    GtkWidget *lbl_hist_title = gtk_label_new("LỊCH SỬ CÁC TRẬN ĐÃ THAM GIA");
    gtk_widget_add_css_class(lbl_hist_title, "card-title");
    gtk_box_append(GTK_BOX(card_history), lbl_hist_title);

    /* Header bảng lịch sử */
    GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(header_row, "table-header");
    GtkWidget *h_id = gtk_label_new("Mã trận");
    gtk_widget_set_size_request(h_id, 110, -1);
    gtk_widget_set_halign(h_id, GTK_ALIGN_START);
    GtkWidget *h_st = gtk_label_new("Trạng thái");
    gtk_widget_set_size_request(h_st, 90, -1);
    gtk_widget_set_halign(h_st, GTK_ALIGN_START);
    GtkWidget *h_rk = gtk_label_new("Thành tích");
    gtk_widget_set_hexpand(h_rk, TRUE);
    gtk_widget_set_halign(h_rk, GTK_ALIGN_START);
    GtkWidget *h_tot = gtk_label_new("Tổng điểm");
    gtk_widget_set_size_request(h_tot, 120, -1);
    gtk_widget_set_halign(h_tot, GTK_ALIGN_END);

    gtk_box_append(GTK_BOX(header_row), h_id);
    gtk_box_append(GTK_BOX(header_row), h_st);
    gtk_box_append(GTK_BOX(header_row), h_rk);
    gtk_box_append(GTK_BOX(header_row), h_tot);
    gtk_box_append(GTK_BOX(card_history), header_row);

    /* Container danh sách các dòng lịch sử động */
    app->box_history_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(card_history), app->box_history_list);

    /* Phân trang */
    GtkWidget *paging_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(paging_box, 12);

    app->btn_history_prev = gtk_button_new_with_label("◀ Trang trước");
    gtk_widget_add_css_class(app->btn_history_prev, "btn-secondary");
    g_signal_connect(app->btn_history_prev, "clicked", G_CALLBACK(on_history_prev_clicked), app);

    app->lbl_history_page_info = gtk_label_new("Trang 1");
    gtk_widget_set_margin_start(app->lbl_history_page_info, 12);
    gtk_widget_set_margin_end(app->lbl_history_page_info, 12);

    app->btn_history_next = gtk_button_new_with_label("Xem thêm ▶");
    gtk_widget_add_css_class(app->btn_history_next, "btn-secondary");
    g_signal_connect(app->btn_history_next, "clicked", G_CALLBACK(on_history_next_clicked), app);

    app->btn_history_refresh = gtk_button_new_with_label("🔄 Làm mới");
    gtk_widget_add_css_class(app->btn_history_refresh, "btn-secondary");
    g_signal_connect(app->btn_history_refresh, "clicked", G_CALLBACK(on_history_refresh_clicked), app);

    gtk_box_append(GTK_BOX(paging_box), app->btn_history_prev);
    gtk_box_append(GTK_BOX(paging_box), app->lbl_history_page_info);
    gtk_box_append(GTK_BOX(paging_box), app->btn_history_next);
    gtk_box_append(GTK_BOX(paging_box), app->btn_history_refresh);
    gtk_box_append(GTK_BOX(card_history), paging_box);

    gtk_box_append(GTK_BOX(box), card_history);

    /* Khởi tạo hiển thị dữ liệu */
    pr_app_refresh_account_view(app);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    return scrolled;
}
