#include "ui/room.h"
#include "ui/app.h"
#include "ui/network.h"
#include "ui/social.h"
#include <stdio.h>
#include <string.h>

void pr_app_do_room_copy_code(pr_app *app) {
    if (!app || !app->state.in_room || !app->state.room.code[0]) return;
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_set_text(clipboard, app->state.room.code);
        char msg[128];
        snprintf(msg, sizeof(msg), "✓ Đã sao chép mã phòng [%s] vào khay nhớ tạm!", app->state.room.code);
        pr_app_show_notification(app, msg, "info");
    }
}

void pr_app_do_room_ready(pr_app *app) {
    if (!app || !app->state.in_room) return;
    if (app->state.room.self_role == PR_SPECTATOR) {
        pr_app_show_notification(app, "Bạn đang là Khán giả, không thể đổi trạng thái sẵn sàng.", "error");
        return;
    }

    /* Tìm trạng thái ready hiện tại của bản thân */
    uint8_t cur_ready = 0;
    for (size_t i = 0; i < app->state.room.player_count; ++i) {
        if (app->state.room.players[i].user_id == app->state.self_user_id) {
            cur_ready = app->state.room.players[i].ready;
            break;
        }
    }

    uint8_t next_ready = cur_ready ? 0 : 1;
    if (pr_command_ready(&app->state.net, next_ready) < 0) {
        pr_app_show_notification(app, "Không thể gửi trạng thái sẵn sàng.", "error");
        return;
    }
    pr_net_source_notify_send(app);
    pr_app_show_notification(app, next_ready ? "Đang gửi: ĐÃ SẴN SÀNG..." : "Đang gửi: HỦY SẴN SÀNG...", "info");
}

void pr_app_do_room_start(pr_app *app) {
    if (!app || !app->state.in_room) return;
    if (app->state.self_user_id != app->state.room.host_id) {
        pr_app_show_notification(app, "Chỉ chủ phòng (HOST) mới có quyền bắt đầu trận đấu.", "error");
        return;
    }
    if (app->state.room.player_count < 2) {
        pr_app_show_notification(app, "Cần tối thiểu 2 người chơi để bắt đầu trận đấu.", "error");
        return;
    }
    for (size_t i = 0; i < app->state.room.player_count; ++i) {
        if (!app->state.room.players[i].ready) {
            pr_app_show_notification(app, "Chưa thể bắt đầu: Vẫn còn người chơi chưa sẵn sàng!", "error");
            return;
        }
    }

    if (pr_command_empty(&app->state.net, PR_START_GAME) < 0) {
        pr_app_show_notification(app, "Không thể gửi lệnh bắt đầu trận đấu.", "error");
        return;
    }
    pr_net_source_notify_send(app);
    pr_app_show_notification(app, "Đang gửi lệnh bắt đầu trận đấu tới máy chủ...", "info");
}

void pr_app_do_room_leave(pr_app *app) {
    if (!app) return;
    if (app->state.in_room && app->state.conn_status == PR_CONN_CONNECTED) {
        pr_command_empty(&app->state.net, PR_LEAVE_ROOM);
        pr_net_source_notify_send(app);
    }
    pr_ui_state_reset_room(&app->state);
    pr_app_show_screen(app, PR_SCREEN_LOBBY);
    pr_app_show_notification(app, "Đã rời khỏi phòng chờ.", "info");
}

void pr_app_refresh_room_view(pr_app *app) {
    if (!app || !app->lbl_room_code_badge) return;

    /* 1. Header phòng: Mã phòng & Counts */
    char code_buf[64];
    snprintf(code_buf, sizeof(code_buf), "MÃ PHÒNG: %s", app->state.room.code[0] ? app->state.room.code : "------");
    gtk_label_set_text(GTK_LABEL(app->lbl_room_code_badge), code_buf);

    char counts_buf[128];
    snprintf(counts_buf, sizeof(counts_buf), "%u/4 Người chơi (PLAYER)  ·  %u Khán giả (SPECTATOR)",
             app->state.room.player_count, app->state.room.spectator_count);
    if (app->lbl_room_counts) {
        gtk_label_set_text(GTK_LABEL(app->lbl_room_counts), counts_buf);
    }

    /* 2. 4 slots thi đấu */
    int active_ready_count = 0;
    int self_is_ready = 0;
    int is_host = (app->state.self_user_id == app->state.room.host_id && app->state.self_user_id != 0);

    for (size_t i = 0; i < 4; ++i) {
        if (!app->lbl_room_slots[i]) continue;
        char slot_text[256];
        if (i < app->state.room.player_count && app->state.room.players[i].user_id != 0) {
            pr_player_view *p = &app->state.room.players[i];
            int p_is_host = (p->user_id == app->state.room.host_id);
            int p_is_self = (p->user_id == app->state.self_user_id);

            char role_tag[64] = "";
            if (p_is_host && p_is_self) strcpy(role_tag, " (Bạn - HOST)");
            else if (p_is_host) strcpy(role_tag, " (HOST)");
            else if (p_is_self) strcpy(role_tag, " (Bạn)");

            char status_tag[64];
            if (!p->connected) {
                strcpy(status_tag, "[MẤT KẾT NỐI]");
            } else if (p->ready) {
                strcpy(status_tag, "[SẴN SÀNG]");
                active_ready_count++;
            } else {
                strcpy(status_tag, "[CHƯA SẴN SÀNG]");
            }

            if (p_is_self) {
                self_is_ready = p->ready;
            }

            snprintf(slot_text, sizeof(slot_text), "Vị trí %zu: %s%s  —  %s", i + 1, p->name, role_tag, status_tag);
        } else {
            snprintf(slot_text, sizeof(slot_text), "Vị trí %zu: (Chỗ trống - Đang chờ người chơi gia nhập...)", i + 1);
        }
        gtk_label_set_text(GTK_LABEL(app->lbl_room_slots[i]), slot_text);
    }

    int can_start = is_host && (app->state.room.player_count >= 2) && (active_ready_count == app->state.room.player_count);

    /* 3. Khán giả */
    if (app->lbl_room_spectators) {
        char spec_text[512];
        uint32_t sc = app->state.room.spectator_count;
        if (app->state.room.self_role == PR_SPECTATOR && sc == 0) sc = 1;

        if (app->state.room.self_role == PR_SPECTATOR) {
            const char *my_name = app->state.self_username[0] ? app->state.self_username : "Khán giả";
            if (sc <= 1) {
                snprintf(spec_text, sizeof(spec_text), "Khán giả (1 người xem): <b>%s (Bạn)</b>", my_name);
            } else {
                snprintf(spec_text, sizeof(spec_text), "Khán giả (%u người xem): <b>%s (Bạn)</b> và %u khán giả khác",
                         sc, my_name, sc - 1);
            }
        } else if (sc > 0) {
            snprintf(spec_text, sizeof(spec_text), "Có %u khán giả đang theo dõi phòng này", sc);
        } else {
            snprintf(spec_text, sizeof(spec_text), "Chưa có khán giả theo dõi (tối đa 12 người xem)");
        }
        gtk_label_set_markup(GTK_LABEL(app->lbl_room_spectators), spec_text);
    }

    /* 4. Nút Sẵn sàng */
    if (app->btn_room_ready) {
        if (app->state.room.self_role == PR_SPECTATOR) {
            gtk_widget_set_sensitive(app->btn_room_ready, FALSE);
            gtk_button_set_label(GTK_BUTTON(app->btn_room_ready), "Khán giả (Chỉ xem)");
        } else {
            gtk_widget_set_sensitive(app->btn_room_ready, TRUE);
            if (self_is_ready) {
                gtk_button_set_label(GTK_BUTTON(app->btn_room_ready), "Hủy Sẵn Sàng");
                gtk_widget_remove_css_class(app->btn_room_ready, "btn-primary");
                gtk_widget_add_css_class(app->btn_room_ready, "btn-secondary");
            } else {
                gtk_button_set_label(GTK_BUTTON(app->btn_room_ready), "SẴN SÀNG");
                gtk_widget_remove_css_class(app->btn_room_ready, "btn-secondary");
                gtk_widget_add_css_class(app->btn_room_ready, "btn-primary");
            }
        }
    }

    /* 5. Nút Bắt đầu trận đấu & Label gợi ý */
    if (app->btn_room_start) {
        gtk_widget_set_sensitive(app->btn_room_start, can_start);
    }

    if (app->lbl_room_hint) {
        if (!is_host) {
            gtk_label_set_text(GTK_LABEL(app->lbl_room_hint), "Chỉ chủ phòng (HOST) mới có quyền bắt đầu trận đấu.");
        } else if (app->state.room.player_count < 2) {
            gtk_label_set_text(GTK_LABEL(app->lbl_room_hint), "⏳ Cần ít nhất 2 người chơi sẵn sàng để bắt đầu trận đấu.");
        } else if (!can_start) {
            gtk_label_set_text(GTK_LABEL(app->lbl_room_hint), "⏳ Đang chờ tất cả người chơi sẵn sàng...");
        } else {
            gtk_label_set_text(GTK_LABEL(app->lbl_room_hint), "✓ Mọi người đã sẵn sàng! Bấm nút bên dưới để BẮT ĐẦU TRẬN ĐẤU.");
        }
    }
}

static void on_room_copy_code_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_room_copy_code(app);
}

static void on_room_ready_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_room_ready(app);
}

static void on_room_start_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_room_start(app);
}

static void on_room_leave_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_room_leave(app);
}

static void on_room_invite_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    if (app && app->popover_online_players) {
        pr_app_do_list_players(app, 0);
        gtk_popover_popup(GTK_POPOVER(app->popover_online_players));
    }
}

static void on_refresh_online_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    if (app) {
        pr_app_do_list_players(app, 0);
    }
}

GtkWidget *pr_ui_build_room_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    /* Header phòng */
    GtkWidget *room_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->lbl_room_code_badge = gtk_label_new("MÃ PHÒNG: ABC123");
    gtk_widget_add_css_class(app->lbl_room_code_badge, "badge-round");

    app->btn_room_copy_code = gtk_button_new_with_label("📋 Sao chép mã");
    gtk_widget_add_css_class(app->btn_room_copy_code, "btn-secondary");
    g_signal_connect(app->btn_room_copy_code, "clicked", G_CALLBACK(on_room_copy_code_clicked), app);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);

    app->lbl_room_counts = gtk_label_new("3/4 Người chơi (PLAYER)  ·  1 Khán giả (SPECTATOR)");
    gtk_widget_add_css_class(app->lbl_room_counts, "text-muted");

    gtk_box_append(GTK_BOX(room_header), app->lbl_room_code_badge);
    gtk_box_append(GTK_BOX(room_header), app->btn_room_copy_code);
    gtk_box_append(GTK_BOX(room_header), spacer);
    gtk_box_append(GTK_BOX(room_header), app->lbl_room_counts);
    gtk_box_append(GTK_BOX(box), room_header);

    /* Danh sách 4 vị trí thi đấu */
    GtkWidget *card_roster = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_roster, "card");

    GtkWidget *lbl_roster_title = gtk_label_new("DANH SÁCH THI ĐẤU (Tối đa 4 người chơi chính thức)");
    gtk_widget_add_css_class(lbl_roster_title, "card-title");
    gtk_box_append(GTK_BOX(card_roster), lbl_roster_title);

    for (size_t i = 0; i < 4; ++i) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "table-row");
        app->lbl_room_slots[i] = gtk_label_new("");
        gtk_box_append(GTK_BOX(row), app->lbl_room_slots[i]);
        gtk_box_append(GTK_BOX(card_roster), row);
    }
    gtk_box_append(GTK_BOX(box), card_roster);

    /* Card Khán giả */
    GtkWidget *card_spec = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card_spec, "card");

    app->lbl_room_spectators = gtk_label_new("Khán giả: Đang cập nhật...");
    gtk_widget_set_halign(app->lbl_room_spectators, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card_spec), app->lbl_room_spectators);
    gtk_box_append(GTK_BOX(box), card_spec);

    /* Gợi ý bắt đầu trận */
    app->lbl_room_hint = gtk_label_new("Đang chuẩn bị phòng...");
    gtk_widget_add_css_class(app->lbl_room_hint, "text-muted");
    gtk_widget_set_halign(app->lbl_room_hint, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), app->lbl_room_hint);

    /* Thao tác phòng */
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    app->btn_room_ready = gtk_button_new_with_label("SẴN SÀNG");
    gtk_widget_add_css_class(app->btn_room_ready, "btn-primary");
    g_signal_connect(app->btn_room_ready, "clicked", G_CALLBACK(on_room_ready_clicked), app);

    app->btn_room_start = gtk_button_new_with_label("BẮT ĐẦU TRẬN ĐẤU (Chỉ Host)");
    gtk_widget_add_css_class(app->btn_room_start, "btn-action-start");
    g_signal_connect(app->btn_room_start, "clicked", G_CALLBACK(on_room_start_clicked), app);

    GtkWidget *btn_invite = gtk_button_new_with_label("Mời người online");
    gtk_widget_add_css_class(btn_invite, "btn-secondary");

    /* Popover danh sách online */
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, btn_invite);
    GtkWidget *pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(pop_box, 14);
    gtk_widget_set_margin_end(pop_box, 14);
    gtk_widget_set_margin_top(pop_box, 14);
    gtk_widget_set_margin_bottom(pop_box, 14);

    GtkWidget *lbl_pop_title = gtk_label_new("NGƯỜI CHƠI ONLINE NGOÀI PHÒNG");
    gtk_widget_add_css_class(lbl_pop_title, "card-title");
    gtk_box_append(GTK_BOX(pop_box), lbl_pop_title);

    app->box_online_players = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(pop_box), app->box_online_players);

    GtkWidget *btn_pop_ref = gtk_button_new_with_label("🔄 Làm mới danh sách");
    gtk_widget_add_css_class(btn_pop_ref, "btn-secondary");
    g_signal_connect(btn_pop_ref, "clicked", G_CALLBACK(on_refresh_online_clicked), app);
    gtk_box_append(GTK_BOX(pop_box), btn_pop_ref);

    gtk_popover_set_child(GTK_POPOVER(popover), pop_box);
    app->popover_online_players = popover;

    g_signal_connect(btn_invite, "clicked", G_CALLBACK(on_room_invite_btn_clicked), app);

    app->btn_room_leave = gtk_button_new_with_label("🚪 Rời phòng");
    gtk_widget_add_css_class(app->btn_room_leave, "btn-danger");
    g_signal_connect(app->btn_room_leave, "clicked", G_CALLBACK(on_room_leave_clicked), app);

    gtk_box_append(GTK_BOX(actions_box), app->btn_room_ready);
    gtk_box_append(GTK_BOX(actions_box), app->btn_room_start);
    gtk_box_append(GTK_BOX(actions_box), btn_invite);
    gtk_box_append(GTK_BOX(actions_box), app->btn_room_leave);
    gtk_box_append(GTK_BOX(box), actions_box);

    /* Khởi tạo dữ liệu hiển thị lần đầu */
    pr_app_refresh_room_view(app);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    return scrolled;
}
