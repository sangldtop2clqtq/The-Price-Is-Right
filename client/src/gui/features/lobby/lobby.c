#include "ui/lobby.h"
#include "ui/account.h"
#include "ui/app.h"
#include "ui/network.h"
#include "ui/social.h"
#include <stdio.h>
#include <string.h>

static void on_open_account_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_open_account(app);
}

void pr_app_do_logout(pr_app *app) {
    if (!app) return;

    if (app->state.conn_status == PR_CONN_CONNECTED && app->state.net.fd >= 0 && app->state.is_authenticated) {
        pr_command_empty(&app->state.net, PR_LOGOUT);
        pr_net_source_notify_send(app);
    }

    pr_ui_state_reset_session(&app->state);
    if (app->lbl_header_user_badge) {
        gtk_label_set_text(GTK_LABEL(app->lbl_header_user_badge), "Chưa đăng nhập");
    }
    pr_app_show_screen(app, PR_SCREEN_CONNECT);
    pr_app_show_notification(app, "Đã đăng xuất tài khoản.", "info");
}

void pr_app_do_create_room(pr_app *app) {
    if (!app) return;
    if (app->state.conn_status != PR_CONN_CONNECTED || app->state.net.fd < 0) {
        const char *err = "Chưa kết nối tới máy chủ TCP.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }
    if (!app->state.is_authenticated) {
        const char *err = "Bạn cần đăng nhập trước khi tạo phòng.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), "");
    if (pr_command_empty(&app->state.net, PR_CREATE_ROOM) < 0) {
        const char *err = "Không thể gửi yêu cầu tạo phòng.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    pr_net_source_notify_send(app);
    pr_app_show_notification(app, "Đang yêu cầu máy chủ tạo phòng mới...", "info");
}

void pr_app_do_join_room(pr_app *app, const char *code) {
    if (!app) return;
    if (app->state.conn_status != PR_CONN_CONNECTED || app->state.net.fd < 0) {
        const char *err = "Chưa kết nối tới máy chủ TCP.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }
    if (!app->state.is_authenticated) {
        const char *err = "Bạn cần đăng nhập trước khi vào phòng.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    if (!code) code = "";
    while (*code == ' ' || *code == '\t') code++;
    char clean_code[8];
    memset(clean_code, 0, sizeof(clean_code));
    size_t i = 0;
    for (; i < 6 && code[i] && code[i] != ' ' && code[i] != '\t'; ++i) {
        char ch = code[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        clean_code[i] = ch;
    }
    clean_code[i] = '\0';

    if (!pr_valid_room_code(clean_code)) {
        const char *err = "Mã phòng không hợp lệ! Vui lòng nhập đúng 6 ký tự chữ hoa/số (ví dụ: ABC123).";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), "");
    if (pr_command_join(&app->state.net, clean_code) < 0) {
        const char *err = "Không thể gửi yêu cầu tham gia phòng.";
        if (app->lbl_lobby_error) gtk_label_set_text(GTK_LABEL(app->lbl_lobby_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    pr_net_source_notify_send(app);
    char msg[128];
    snprintf(msg, sizeof(msg), "Đang gửi yêu cầu tham gia phòng %s...", clean_code);
    pr_app_show_notification(app, msg, "info");
}

static void on_create_room_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_create_room(app);
}

static void on_join_room_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    const char *code = app->entry_room_code ? gtk_editable_get_text(GTK_EDITABLE(app->entry_room_code)) : "";
    pr_app_do_join_room(app, code);
}

static void on_entry_room_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    pr_app *app = (pr_app *)user_data;
    const char *code = app->entry_room_code ? gtk_editable_get_text(GTK_EDITABLE(app->entry_room_code)) : "";
    pr_app_do_join_room(app, code);
}

static void on_logout_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_logout(app);
}

GtkWidget *pr_ui_build_lobby_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(box, 40);
    gtk_widget_set_margin_end(box, 40);
    gtk_widget_set_margin_top(box, 30);
    gtk_widget_set_margin_bottom(box, 30);

    /* Header chào mừng & nút Đăng xuất */
    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    app->lbl_lobby_welcome = gtk_label_new("Chào mừng bạn đến với Sảnh Chờ!");
    gtk_widget_add_css_class(app->lbl_lobby_welcome, "app-title");
    gtk_widget_set_halign(app->lbl_lobby_welcome, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(top_row), app->lbl_lobby_welcome);

    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(top_spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), top_spacer);

    app->btn_logout = gtk_button_new_with_label("🚪 Đăng xuất");
    gtk_widget_add_css_class(app->btn_logout, "btn-secondary");
    g_signal_connect(app->btn_logout, "clicked", G_CALLBACK(on_logout_clicked), app);
    gtk_box_append(GTK_BOX(top_row), app->btn_logout);

    gtk_box_append(GTK_BOX(box), top_row);

    /* Label lỗi sảnh */
    app->lbl_lobby_error = gtk_label_new("");
    gtk_widget_add_css_class(app->lbl_lobby_error, "text-danger");
    gtk_label_set_wrap(GTK_LABEL(app->lbl_lobby_error), TRUE);
    gtk_widget_set_halign(app->lbl_lobby_error, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), app->lbl_lobby_error);

    /* Grid tạo phòng & vào phòng */
    GtkWidget *grid_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);

    /* Card tạo phòng */
    GtkWidget *card_create = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card_create, "card");
    gtk_widget_set_hexpand(card_create, TRUE);

    GtkWidget *lbl_c_title = gtk_label_new("TẠO PHÒNG CHƠI MỚI");
    gtk_widget_add_css_class(lbl_c_title, "card-title");
    gtk_box_append(GTK_BOX(card_create), lbl_c_title);

    GtkWidget *lbl_c_desc = gtk_label_new("Mở một phòng chờ mới, bạn sẽ là Host và có quyền bắt đầu trận đấu khi người chơi đã sẵn sàng.");
    gtk_label_set_wrap(GTK_LABEL(lbl_c_desc), TRUE);
    gtk_box_append(GTK_BOX(card_create), lbl_c_desc);

    app->btn_create_room = gtk_button_new_with_label("+ Tạo phòng ngay");
    gtk_widget_add_css_class(app->btn_create_room, "btn-primary");
    g_signal_connect(app->btn_create_room, "clicked", G_CALLBACK(on_create_room_clicked), app);
    gtk_box_append(GTK_BOX(card_create), app->btn_create_room);

    /* Card vào phòng */
    GtkWidget *card_join = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card_join, "card");
    gtk_widget_set_hexpand(card_join, TRUE);

    GtkWidget *lbl_j_title = gtk_label_new("VÀO BẰNG MÃ PHÒNG");
    gtk_widget_add_css_class(lbl_j_title, "card-title");
    gtk_box_append(GTK_BOX(card_join), lbl_j_title);

    app->entry_room_code = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_room_code), "Nhập mã phòng 6 ký tự (ví dụ: ABC123)");
    g_signal_connect(app->entry_room_code, "activate", G_CALLBACK(on_entry_room_activate), app);
    gtk_box_append(GTK_BOX(card_join), app->entry_room_code);

    app->btn_join_room = gtk_button_new_with_label("Vào phòng (Enter)");
    gtk_widget_add_css_class(app->btn_join_room, "btn-secondary");
    g_signal_connect(app->btn_join_room, "clicked", G_CALLBACK(on_join_room_clicked), app);
    gtk_box_append(GTK_BOX(card_join), app->btn_join_room);

    gtk_box_append(GTK_BOX(grid_actions), card_create);
    gtk_box_append(GTK_BOX(grid_actions), card_join);
    gtk_box_append(GTK_BOX(box), grid_actions);

    /* Card tính năng cá nhân & cộng đồng */
    GtkWidget *card_features = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_features, "card");

    GtkWidget *lbl_feat_title = gtk_label_new("TÍNH NĂNG CÁ NHÂN & CỘNG ĐỒNG");
    gtk_widget_add_css_class(lbl_feat_title, "card-title");
    gtk_box_append(GTK_BOX(card_features), lbl_feat_title);

    GtkWidget *box_feat_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *btn_hist = gtk_button_new_with_label("Lịch sử đấu");
    gtk_widget_add_css_class(btn_hist, "btn-secondary");
    g_signal_connect(btn_hist, "clicked", G_CALLBACK(on_open_account_clicked), app);

    GtkWidget *btn_stat = gtk_button_new_with_label("Thống kê cá nhân");
    gtk_widget_add_css_class(btn_stat, "btn-secondary");
    g_signal_connect(btn_stat, "clicked", G_CALLBACK(on_open_account_clicked), app);

    gtk_box_append(GTK_BOX(box_feat_btns), btn_hist);
    gtk_box_append(GTK_BOX(box_feat_btns), btn_stat);
    gtk_box_append(GTK_BOX(card_features), box_feat_btns);
    gtk_box_append(GTK_BOX(box), card_features);

    /* Card lời mời (Mốc UI-08) */
    GtkWidget *card_invites = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card_invites, "card");

    GtkWidget *lbl_inv_title = gtk_label_new("Lời mời thi đấu gần đây");
    gtk_widget_add_css_class(lbl_inv_title, "card-title");
    gtk_box_append(GTK_BOX(card_invites), lbl_inv_title);

    app->box_lobby_invites = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(card_invites), app->box_lobby_invites);
    gtk_box_append(GTK_BOX(box), card_invites);

    pr_app_refresh_invitations_view(app);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    return scrolled;
}
