#include "ui/gameplay.h"
#include "ui/app.h"
#include "ui/common.h"
#include "ui/network.h"
#include <stdio.h>
#include <string.h>

static gboolean on_game_countdown_tick(gpointer user_data) {
    pr_app *app = (pr_app *)user_data;
    if (!app || !app->state.in_game) {
        app->countdown_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double now = pr_client_now();
    double remaining = app->state.local_deadline - now;
    if (remaining < 0.0) remaining = 0.0;
    uint32_t sec = (uint32_t)remaining;
    app->state.remaining_seconds = sec;

    if (app->lbl_game_countdown_badge) {
        char cd_buf[64];
        snprintf(cd_buf, sizeof(cd_buf), "Còn %02u:%02u", sec / 60, sec % 60);
        gtk_label_set_text(GTK_LABEL(app->lbl_game_countdown_badge), cd_buf);
    }

    if (sec == 0) {
        if (app->entry_game_guess) gtk_widget_set_sensitive(app->entry_game_guess, FALSE);
        if (app->btn_game_submit_guess) gtk_widget_set_sensitive(app->btn_game_submit_guess, FALSE);
        if (app->lbl_game_ack_status) {
            gtk_label_set_text(GTK_LABEL(app->lbl_game_ack_status), "Hết giờ nộp giá! Đang chờ máy chủ tổng kết...");
        }
        app->countdown_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

void pr_app_start_countdown(pr_app *app, uint32_t remaining_ms) {
    if (!app) return;
    pr_app_stop_countdown(app);
    app->state.remaining_seconds = remaining_ms / 1000;
    app->state.local_deadline = pr_client_now() + ((double)remaining_ms / 1000.0);
    app->countdown_timer_id = g_timeout_add(250, on_game_countdown_tick, app);
}

void pr_app_stop_countdown(pr_app *app) {
    if (!app) return;
    if (app->countdown_timer_id != 0) {
        g_source_remove(app->countdown_timer_id);
        app->countdown_timer_id = 0;
    }
}

void pr_app_do_game_submit_guess(pr_app *app) {
    if (!app || !app->state.in_game) return;
    if (app->state.room.self_role == PR_SPECTATOR) {
        pr_app_show_notification(app, "Khán giả không thể gửi dự đoán giá.", "error");
        return;
    }
    if (app->state.remaining_seconds == 0) {
        pr_app_show_notification(app, "Vòng chơi đã hết thời gian nộp giá!", "error");
        return;
    }

    const char *text = app->entry_game_guess ? gtk_editable_get_text(GTK_EDITABLE(app->entry_game_guess)) : "";
    if (!text || !*text) {
        pr_app_show_notification(app, "Vui lòng nhập số tiền bạn dự đoán.", "error");
        return;
    }

    uint64_t amount = 0;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] >= '0' && text[i] <= '9') {
            amount = amount * 10 + (uint64_t)(text[i] - '0');
            if (amount > UINT64_C(1000000000000)) {
                pr_app_show_notification(app, "Giá trị dự đoán vượt quá giới hạn 1.000.000.000.000 ₫!", "error");
                return;
            }
        }
    }

    if (amount == 0) {
        pr_app_show_notification(app, "Giá trị dự đoán phải lớn hơn 0 ₫.", "error");
        return;
    }

    app->state.pending_guess_amount = amount;
    if (pr_command_guess(&app->state.net, app->state.game.match_id, app->state.game.round_no, amount) < 0) {
        pr_app_show_notification(app, "Không thể gửi dự đoán giá (kết nối hoặc hàng đợi TX lỗi).", "error");
        return;
    }
    pr_net_source_notify_send(app);

    char vnd_str[64], ack_msg[128];
    pr_format_vnd(amount, vnd_str, sizeof(vnd_str));
    snprintf(ack_msg, sizeof(ack_msg), "Đang gửi cập nhật: %s...", vnd_str);
    if (app->lbl_game_ack_status) {
        gtk_label_set_text(GTK_LABEL(app->lbl_game_ack_status), ack_msg);
    }
    pr_app_show_notification(app, ack_msg, "info");
}

void pr_app_refresh_game_view(pr_app *app) {
    if (!app || !app->lbl_game_round_badge) return;

    /* 1. Header vòng: Badge Round & Countdown */
    char r_buf[128];
    const char *type_name = (app->state.game.type == PR_SINGLE) ? "SẢN PHẨM ĐƠN (SINGLE)" : "GIỎ HÀNG (BASKET)";
    snprintf(r_buf, sizeof(r_buf), "VÒNG %u/5 · %s", app->state.game.round_no, type_name);
    gtk_label_set_text(GTK_LABEL(app->lbl_game_round_badge), r_buf);

    char cd_buf[64];
    uint32_t sec = app->state.remaining_seconds;
    snprintf(cd_buf, sizeof(cd_buf), "Còn %02u:%02u", sec / 60, sec % 60);
    if (app->lbl_game_countdown_badge) {
        gtk_label_set_text(GTK_LABEL(app->lbl_game_countdown_badge), cd_buf);
    }

    /* 2. Render Cards Sản phẩm động */
    if (app->box_game_products) {
        GtkWidget *child;
        while ((child = gtk_widget_get_first_child(app->box_game_products)) != NULL) {
            gtk_box_remove(GTK_BOX(app->box_game_products), child);
        }

        uint16_t n_items = app->state.game.item_count;
        if (n_items == 0) n_items = 1;
        for (uint16_t i = 0; i < n_items && i < PR_MAX_ITEMS; ++i) {
            pr_item *item = &app->state.game.items[i];

            GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_widget_add_css_class(card, "product-card");
            gtk_widget_set_hexpand(card, TRUE);

            GtkWidget *img_box = pr_ui_product_image(item->image_key);

            GtkWidget *name_lbl = gtk_label_new(item->name[0] ? item->name : "Sản phẩm mẫu");
            gtk_widget_add_css_class(name_lbl, "product-name");
            gtk_label_set_wrap(GTK_LABEL(name_lbl), TRUE);

            char qty_buf[64];
            if (app->state.game.type == PR_BASKET) {
                snprintf(qty_buf, sizeof(qty_buf), "Số lượng trong giỏ: %u", item->quantity ? item->quantity : 1);
            } else {
                snprintf(qty_buf, sizeof(qty_buf), "Quy cách: Sản phẩm đơn lẻ");
            }
            GtkWidget *qty_lbl = gtk_label_new(qty_buf);
            gtk_widget_add_css_class(qty_lbl, "product-qty");

            gtk_box_append(GTK_BOX(card), img_box);
            gtk_box_append(GTK_BOX(card), name_lbl);
            gtk_box_append(GTK_BOX(card), qty_lbl);

            gtk_box_append(GTK_BOX(app->box_game_products), card);
        }
    }

    /* 3. Khu vực nộp giá hoặc theo dõi của Spectator */
    int is_spectator = (app->state.room.self_role == PR_SPECTATOR);
    if (app->box_game_input_area) {
        gtk_widget_set_visible(app->box_game_input_area, !is_spectator);
    }
    if (app->lbl_game_spectator_notice) {
        gtk_widget_set_visible(app->lbl_game_spectator_notice, FALSE);
    }
    if (app->box_game_spectator_monitor) {
        gtk_widget_set_visible(app->box_game_spectator_monitor, is_spectator);
        if (is_spectator) {
            for (size_t i = 0; i < PR_MAX_PLAYERS; ++i) {
                if (!app->lbl_game_spectator_guesses[i]) continue;
                GtkWidget *p_row = gtk_widget_get_parent(app->lbl_game_spectator_guesses[i]);
                if (i < app->state.game.count && app->state.game.participants[i].user_id != 0) {
                    pr_participant *part = &app->state.game.participants[i];
                    char g_buf[256];
                    if (app->state.player_submitted[i] && app->state.player_guess_amounts[i] > 0) {
                        char vnd_str[64];
                        pr_format_vnd(app->state.player_guess_amounts[i], vnd_str, sizeof(vnd_str));
                        snprintf(g_buf, sizeof(g_buf),
                                 "<b>%s</b>:   <span color=\"#34d399\"><b>ĐÃ NỘP DỰ ĐOÁN: %s</b></span>",
                                 part->name, vnd_str);
                    } else {
                        snprintf(g_buf, sizeof(g_buf),
                                 "<b>%s</b>:   <span color=\"#94a3b8\">Đang suy nghĩ và nhập giá...</span>",
                                 part->name);
                    }
                    gtk_label_set_markup(GTK_LABEL(app->lbl_game_spectator_guesses[i]), g_buf);
                    gtk_widget_set_visible(app->lbl_game_spectator_guesses[i], TRUE);
                    if (p_row) gtk_widget_set_visible(p_row, TRUE);
                } else {
                    gtk_widget_set_visible(app->lbl_game_spectator_guesses[i], FALSE);
                    if (p_row) gtk_widget_set_visible(p_row, FALSE);
                }
            }
        }
    }

    if (!is_spectator) {
        if (app->lbl_game_input_prompt) {
            if (app->state.game.type == PR_BASKET) {
                gtk_label_set_text(GTK_LABEL(app->lbl_game_input_prompt), "Bạn dự đoán TỔNG GIÁ TRỊ của cả giỏ hàng trên là bao nhiêu?");
            } else {
                gtk_label_set_text(GTK_LABEL(app->lbl_game_input_prompt), "Bạn dự đoán GIÁ của sản phẩm trên là bao nhiêu?");
            }
        }

        if (app->lbl_game_ack_status) {
            if (app->state.has_acked_guess) {
                char ack_buf[128], vnd_buf[64];
                pr_format_vnd(app->state.acked_guess_amount, vnd_buf, sizeof(vnd_buf));
                snprintf(ack_buf, sizeof(ack_buf), "Máy chủ đã ghi nhận giá: %s (Có thể sửa trước 00:00)", vnd_buf);
                gtk_label_set_text(GTK_LABEL(app->lbl_game_ack_status), ack_buf);
            } else {
                gtk_label_set_text(GTK_LABEL(app->lbl_game_ack_status), "Chưa nộp dự đoán giá cho vòng này.");
            }
        }
    }

    /* 4. Cột bên phải: Bảng điểm trực tiếp */
    if (app->state.game.count == 0 && app->state.room.player_count > 0) {
        app->state.game.count = app->state.room.player_count;
        for (size_t i = 0; i < app->state.room.player_count && i < PR_MAX_PLAYERS; ++i) {
            app->state.game.participants[i].user_id = app->state.room.players[i].user_id;
            g_strlcpy(app->state.game.participants[i].name, app->state.room.players[i].name, sizeof(app->state.game.participants[i].name));
            app->state.game.participants[i].connected = 1;
        }
    }

    for (size_t i = 0; i < PR_MAX_PLAYERS; ++i) {
        if (!app->lbl_game_scoreboard_rows[i]) continue;
        GtkWidget *parent_row = gtk_widget_get_parent(app->lbl_game_scoreboard_rows[i]);
        if (i < app->state.game.count && app->state.game.participants[i].user_id != 0) {
            pr_participant *part = &app->state.game.participants[i];
            int is_self = (part->user_id == app->state.self_user_id);
            char row_buf[512];
            char guess_sub_buf[128] = "";

            /* Khán giả được xem trực tiếp giá nộp của người chơi */
            if (is_spectator) {
                if (app->state.player_submitted[i] && app->state.player_guess_amounts[i] > 0) {
                    char vnd_str[64];
                    pr_format_vnd(app->state.player_guess_amounts[i], vnd_str, sizeof(vnd_str));
                    snprintf(guess_sub_buf, sizeof(guess_sub_buf),
                             "\n  <span color=\"#34d399\">✓ Đoán: <b>%s</b></span>", vnd_str);
                } else {
                    snprintf(guess_sub_buf, sizeof(guess_sub_buf),
                             "\n  <span color=\"#64748b\">⏳ Đang suy nghĩ...</span>");
                }
            } else if (is_self && app->state.has_acked_guess) {
                char vnd_str[64];
                pr_format_vnd(app->state.acked_guess_amount, vnd_str, sizeof(vnd_str));
                snprintf(guess_sub_buf, sizeof(guess_sub_buf),
                         "\n  <span color=\"#34d399\">✓ Giá của bạn: <b>%s</b></span>", vnd_str);
            }

            snprintf(row_buf, sizeof(row_buf),
                     "<b>%zu.</b> %s%s   —   <span color=\"#fbbf24\"><b>%u điểm</b></span>%s",
                     i + 1,
                     part->name[0] ? part->name : "Người chơi",
                     is_self ? " <span color=\"#2dd4bf\">(Bạn)</span>" : "",
                     app->state.game.totals[i],
                     guess_sub_buf);
            gtk_label_set_markup(GTK_LABEL(app->lbl_game_scoreboard_rows[i]), row_buf);
            gtk_widget_set_visible(app->lbl_game_scoreboard_rows[i], TRUE);
            if (parent_row) gtk_widget_set_visible(parent_row, TRUE);
        } else {
            gtk_widget_set_visible(app->lbl_game_scoreboard_rows[i], FALSE);
            if (parent_row) gtk_widget_set_visible(parent_row, FALSE);
        }
    }

    if (app->lbl_game_spectator_count) {
        char spec_buf[512];
        uint32_t sc = app->state.room.spectator_count;
        if (app->state.room.self_role == PR_SPECTATOR && sc == 0) sc = 1;

        if (app->state.room.self_role == PR_SPECTATOR) {
            const char *my_name = app->state.self_username[0] ? app->state.self_username : "Khán giả";
            if (sc <= 1) {
                snprintf(spec_buf, sizeof(spec_buf), "Khán giả đang xem (1):\n<b>%s (Bạn)</b>", my_name);
            } else {
                snprintf(spec_buf, sizeof(spec_buf), "Khán giả đang xem (%u):\n<b>%s (Bạn)</b> và %u khán giả khác",
                         sc, my_name, sc - 1);
            }
        } else if (sc > 0) {
            snprintf(spec_buf, sizeof(spec_buf), "Khán giả đang xem: %u người", sc);
        } else {
            snprintf(spec_buf, sizeof(spec_buf), "Khán giả: Chưa có người xem");
        }
        gtk_label_set_markup(GTK_LABEL(app->lbl_game_spectator_count), spec_buf);
    }
}

static void on_game_submit_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_game_submit_guess(app);
}

static void on_game_entry_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_game_submit_guess(app);
}

GtkWidget *pr_ui_build_game_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *main_h_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_margin_start(main_h_box, 30);
    gtk_widget_set_margin_end(main_h_box, 30);
    gtk_widget_set_margin_top(main_h_box, 20);
    gtk_widget_set_margin_bottom(main_h_box, 20);

    /* Cột bên trái: Thông tin vòng, Sản phẩm, Ô đoán giá */
    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(left_col, TRUE);

    /* Header vòng đấu */
    GtkWidget *round_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->lbl_game_round_badge = gtk_label_new("VÒNG 4/5 · GIỎ HÀNG (BASKET)");
    gtk_widget_add_css_class(app->lbl_game_round_badge, "badge-round");

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);

    app->lbl_game_countdown_badge = gtk_label_new("Còn 00:23");
    gtk_widget_add_css_class(app->lbl_game_countdown_badge, "badge-countdown");

    gtk_box_append(GTK_BOX(round_header), app->lbl_game_round_badge);
    gtk_box_append(GTK_BOX(round_header), spacer);
    gtk_box_append(GTK_BOX(round_header), app->lbl_game_countdown_badge);
    gtk_box_append(GTK_BOX(left_col), round_header);

    /* Danh sách thẻ sản phẩm (Render động) */
    app->box_game_products = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(left_col), app->box_game_products);

    /* Thẻ nộp giá đoán */
    GtkWidget *card_guess = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_guess, "card");

    /* Khu vực theo dõi trực tiếp dành cho Khán giả */
    app->box_game_spectator_monitor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *mon_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_mon_badge = gtk_label_new("CHẾ ĐỘ KHÁN GIẢ · THEO DÕI TRỰC TIẾP");
    gtk_widget_add_css_class(lbl_mon_badge, "badge-connected");
    gtk_box_append(GTK_BOX(mon_hdr), lbl_mon_badge);
    gtk_box_append(GTK_BOX(app->box_game_spectator_monitor), mon_hdr);

    GtkWidget *lbl_mon_title = gtk_label_new("DỰ ĐOÁN GIÁ CỦA CÁC THÍ SINH ĐANG THI ĐẤU");
    gtk_widget_add_css_class(lbl_mon_title, "card-title");
    gtk_box_append(GTK_BOX(app->box_game_spectator_monitor), lbl_mon_title);

    GtkWidget *lbl_mon_sub = gtk_label_new("Giá dự đoán của từng người chơi sẽ được cập nhật trực tiếp tại đây ngay khi họ nộp bài:");
    gtk_widget_add_css_class(lbl_mon_sub, "text-muted");
    gtk_box_append(GTK_BOX(app->box_game_spectator_monitor), lbl_mon_sub);

    for (size_t i = 0; i < PR_MAX_PLAYERS; ++i) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "table-row");
        app->lbl_game_spectator_guesses[i] = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(app->lbl_game_spectator_guesses[i]), 0.0f);
        gtk_box_append(GTK_BOX(row), app->lbl_game_spectator_guesses[i]);
        gtk_box_append(GTK_BOX(app->box_game_spectator_monitor), row);
    }
    gtk_box_append(GTK_BOX(card_guess), app->box_game_spectator_monitor);

    /* Thông báo cho Khán giả (ẩn khi là Player) */
    app->lbl_game_spectator_notice = gtk_label_new("Bạn đang theo dõi trận đấu với tư cách Khán giả (không thể gửi dự đoán giá).");
    gtk_widget_add_css_class(app->lbl_game_spectator_notice, "text-muted");
    gtk_widget_set_halign(app->lbl_game_spectator_notice, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card_guess), app->lbl_game_spectator_notice);

    /* Box nhập giá cho người chơi chính thức */
    app->box_game_input_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    app->lbl_game_input_prompt = gtk_label_new("Bạn dự đoán tổng giá trị giỏ hàng trên là bao nhiêu?");
    gtk_widget_add_css_class(app->lbl_game_input_prompt, "card-title");
    gtk_box_append(GTK_BOX(app->box_game_input_area), app->lbl_game_input_prompt);

    GtkWidget *guess_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->entry_game_guess = gtk_entry_new();
    gtk_widget_add_css_class(app->entry_game_guess, "price-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_game_guess), "Nhập số tiền dự đoán (ví dụ: 450000)");
    gtk_widget_set_hexpand(app->entry_game_guess, TRUE);
    g_signal_connect(app->entry_game_guess, "activate", G_CALLBACK(on_game_entry_activate), app);

    GtkWidget *lbl_currency = gtk_label_new("VND");
    gtk_widget_add_css_class(lbl_currency, "card-title");

    app->btn_game_submit_guess = gtk_button_new_with_label("GỬI DỰ ĐOÁN (Enter)");
    gtk_widget_add_css_class(app->btn_game_submit_guess, "btn-primary");
    g_signal_connect(app->btn_game_submit_guess, "clicked", G_CALLBACK(on_game_submit_clicked), app);

    gtk_box_append(GTK_BOX(guess_input_box), app->entry_game_guess);
    gtk_box_append(GTK_BOX(guess_input_box), lbl_currency);
    gtk_box_append(GTK_BOX(guess_input_box), app->btn_game_submit_guess);
    gtk_box_append(GTK_BOX(app->box_game_input_area), guess_input_box);

    /* Thông tin giá server đã nhận */
    app->lbl_game_ack_status = gtk_label_new("Chưa nộp dự đoán giá cho vòng này.");
    gtk_widget_add_css_class(app->lbl_game_ack_status, "badge-connected");
    gtk_box_append(GTK_BOX(app->box_game_input_area), app->lbl_game_ack_status);

    GtkWidget *lbl_hint = gtk_label_new("Gợi ý: Bạn có thể sửa và gửi lại giá nhiều lần cho đến khi hết thời gian vòng.");
    gtk_widget_add_css_class(lbl_hint, "text-muted");
    gtk_box_append(GTK_BOX(app->box_game_input_area), lbl_hint);

    gtk_box_append(GTK_BOX(card_guess), app->box_game_input_area);
    gtk_box_append(GTK_BOX(left_col), card_guess);
    gtk_box_append(GTK_BOX(main_h_box), left_col);

    /* Cột bên phải: Bảng điểm trực tiếp */
    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(right_col, "card");
    gtk_widget_set_size_request(right_col, 280, -1);

    GtkWidget *lbl_sb_title = gtk_label_new("BẢNG ĐIỂM HIỆN TẠI");
    gtk_widget_add_css_class(lbl_sb_title, "card-title");
    gtk_box_append(GTK_BOX(right_col), lbl_sb_title);

    for (size_t i = 0; i < PR_MAX_PLAYERS; ++i) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "table-row");
        app->lbl_game_scoreboard_rows[i] = gtk_label_new("");
        gtk_box_append(GTK_BOX(row), app->lbl_game_scoreboard_rows[i]);
        gtk_box_append(GTK_BOX(right_col), row);
    }

    app->lbl_game_spectator_count = gtk_label_new("Khán giả đang xem: 0");
    gtk_widget_add_css_class(app->lbl_game_spectator_count, "text-muted");
    gtk_widget_set_margin_top(app->lbl_game_spectator_count, 16);
    gtk_box_append(GTK_BOX(right_col), app->lbl_game_spectator_count);

    gtk_box_append(GTK_BOX(main_h_box), right_col);

    /* Khởi tạo hiển thị Gameplay ban đầu */
    pr_app_refresh_game_view(app);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), main_h_box);
    return scrolled;
}
