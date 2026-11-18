#include "ui/auth.h"
#include "ui/app.h"
#include "ui/network.h"
#include <stdio.h>
#include <string.h>

void pr_app_set_auth_mode(pr_app *app, int is_register) {
    if (!app) return;
    app->is_register_mode = is_register;

    if (app->box_confirm_password) {
        gtk_widget_set_visible(app->box_confirm_password, is_register ? TRUE : FALSE);
    }

    if (app->btn_tab_login && app->btn_tab_register) {
        if (is_register) {
            gtk_widget_remove_css_class(app->btn_tab_login, "tab-active");
            gtk_widget_add_css_class(app->btn_tab_register, "tab-active");
        } else {
            gtk_widget_remove_css_class(app->btn_tab_register, "tab-active");
            gtk_widget_add_css_class(app->btn_tab_login, "tab-active");
        }
    }

    if (app->btn_login_submit) {
        gtk_widget_set_sensitive(app->btn_login_submit, TRUE);
        gtk_button_set_label(GTK_BUTTON(app->btn_login_submit),
                             is_register ? "Hoàn tất Đăng ký (Enter)" : "Đăng nhập ngay (Enter)");
    }

    if (app->lbl_auth_error) {
        gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), "");
    }
}

void pr_app_do_auth_submit(pr_app *app) {
    if (!app) return;

    if (app->state.conn_status != PR_CONN_CONNECTED || app->state.net.fd < 0) {
        if (app->state.conn_status == PR_CONN_CONNECTING) {
            const char *msg = "Đang kết nối tới máy chủ (127.0.0.1:6666), vui lòng thử lại sau giây lát...";
            if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), msg);
            pr_app_show_notification(app, msg, "info");
            return;
        }
        /* Tự động kích hoạt kết nối lại tới máy chủ 127.0.0.1:6666 */
        pr_app_connect(app, "127.0.0.1", 6666);
        const char *err = "Chưa kết nối được tới máy chủ (127.0.0.1:6666). Đang tự động kết nối lại...";
        if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    const char *username = app->entry_username ? gtk_editable_get_text(GTK_EDITABLE(app->entry_username)) : "";
    const char *password = app->entry_password ? gtk_editable_get_text(GTK_EDITABLE(app->entry_password)) : "";

    size_t ulen = strlen(username);
    if (ulen < 3 || ulen > 32) {
        const char *err = "Tên tài khoản phải từ 3 đến 32 ký tự.";
        if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    size_t plen = strlen(password);
    if (plen < 8 || plen > 128) {
        const char *err = "Mật khẩu phải từ 8 đến 128 ký tự.";
        if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    if (app->is_register_mode) {
        const char *confirm = app->entry_password_confirm ? gtk_editable_get_text(GTK_EDITABLE(app->entry_password_confirm)) : "";
        if (strcmp(password, confirm) != 0) {
            const char *err = "Mật khẩu xác nhận không trùng khớp!";
            if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), err);
            pr_app_show_notification(app, err, "error");
            return;
        }
    }

    if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), "");
    if (app->btn_login_submit) {
        gtk_widget_set_sensitive(app->btn_login_submit, FALSE);
        gtk_button_set_label(GTK_BUTTON(app->btn_login_submit),
                             app->is_register_mode ? "Đang xử lý đăng ký..." : "Đang xử lý đăng nhập...");
    }

    uint16_t code = app->is_register_mode ? PR_REGISTER : PR_LOGIN;
    if (pr_command_auth(&app->state.net, code, username, password) < 0) {
        if (app->btn_login_submit) {
            gtk_widget_set_sensitive(app->btn_login_submit, TRUE);
            gtk_button_set_label(GTK_BUTTON(app->btn_login_submit),
                                 app->is_register_mode ? "Hoàn tất Đăng ký (Enter)" : "Đăng nhập ngay (Enter)");
        }
        const char *err = "Gửi yêu cầu thất bại (hàng đợi TX đầy hoặc kết nối lỗi).";
        if (app->lbl_auth_error) gtk_label_set_text(GTK_LABEL(app->lbl_auth_error), err);
        pr_app_show_notification(app, err, "error");
        return;
    }

    pr_net_source_notify_send(app);
    pr_app_show_notification(app,
                             app->is_register_mode ? "Đang gửi thông tin đăng ký lên máy chủ..." : "Đang xác thực thông tin đăng nhập...",
                             "info");
}

static void on_tab_login_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_set_auth_mode(app, 0);
}

static void on_tab_register_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_set_auth_mode(app, 1);
}

static void on_auth_submit_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_auth_submit(app);
}

static void on_entry_auth_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    pr_app *app = (pr_app *)user_data;
    pr_app_do_auth_submit(app);
}

GtkWidget *pr_ui_build_auth_page(pr_app *app) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(center_box, 40);
    gtk_widget_set_margin_bottom(center_box, 40);

    /* Bỏ thẻ cấu hình TCP trên UI, kết nối trực tiếp ngầm với server 127.0.0.1:6666 */
    app->entry_server_ip = NULL;
    app->entry_server_port = NULL;
    app->btn_connect = NULL;
    app->btn_ping = NULL;

    /* Thẻ đăng nhập / đăng ký */
    GtkWidget *card_auth = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card_auth, "card");
    gtk_widget_set_size_request(card_auth, 460, -1);

    GtkWidget *lbl_auth_title = gtk_label_new("ĐĂNG NHẬP / ĐĂNG KÝ TÀI KHOẢN");
    gtk_widget_add_css_class(lbl_auth_title, "card-title");
    gtk_box_append(GTK_BOX(card_auth), lbl_auth_title);

    /* Tab Switcher */
    GtkWidget *tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(tab_bar, "tab-bar");

    app->btn_tab_login = gtk_button_new_with_label("Đăng nhập");
    gtk_widget_add_css_class(app->btn_tab_login, "tab-btn");
    gtk_widget_add_css_class(app->btn_tab_login, "tab-active");
    gtk_widget_set_hexpand(app->btn_tab_login, TRUE);
    g_signal_connect(app->btn_tab_login, "clicked", G_CALLBACK(on_tab_login_clicked), app);

    app->btn_tab_register = gtk_button_new_with_label("Tạo tài khoản mới");
    gtk_widget_add_css_class(app->btn_tab_register, "tab-btn");
    gtk_widget_set_hexpand(app->btn_tab_register, TRUE);
    g_signal_connect(app->btn_tab_register, "clicked", G_CALLBACK(on_tab_register_clicked), app);

    gtk_box_append(GTK_BOX(tab_bar), app->btn_tab_login);
    gtk_box_append(GTK_BOX(tab_bar), app->btn_tab_register);
    gtk_box_append(GTK_BOX(card_auth), tab_bar);

    /* Tên tài khoản */
    GtkWidget *lbl_u = gtk_label_new("Tên tài khoản (3 - 32 ký tự):");
    gtk_widget_add_css_class(lbl_u, "form-label");
    gtk_widget_set_halign(lbl_u, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card_auth), lbl_u);

    app->entry_username = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_username), "Ví dụ: lequoctuan");
    g_signal_connect(app->entry_username, "activate", G_CALLBACK(on_entry_auth_activate), app);
    gtk_box_append(GTK_BOX(card_auth), app->entry_username);

    /* Mật khẩu */
    GtkWidget *lbl_p = gtk_label_new("Mật khẩu (8 - 128 ký tự):");
    gtk_widget_add_css_class(lbl_p, "form-label");
    gtk_widget_set_halign(lbl_p, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card_auth), lbl_p);

    app->entry_password = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(app->entry_password), TRUE);
    g_signal_connect(app->entry_password, "activate", G_CALLBACK(on_entry_auth_activate), app);
    gtk_box_append(GTK_BOX(card_auth), app->entry_password);

    /* Xác nhận mật khẩu (Chế độ đăng ký) */
    app->box_confirm_password = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *lbl_cp = gtk_label_new("Xác nhận lại mật khẩu:");
    gtk_widget_add_css_class(lbl_cp, "form-label");
    gtk_widget_set_halign(lbl_cp, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(app->box_confirm_password), lbl_cp);

    app->entry_password_confirm = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(app->entry_password_confirm), TRUE);
    g_signal_connect(app->entry_password_confirm, "activate", G_CALLBACK(on_entry_auth_activate), app);
    gtk_box_append(GTK_BOX(app->box_confirm_password), app->entry_password_confirm);

    gtk_widget_set_visible(app->box_confirm_password, FALSE);
    gtk_box_append(GTK_BOX(card_auth), app->box_confirm_password);

    /* Nút Submit */
    app->btn_login_submit = gtk_button_new_with_label("Đăng nhập ngay (Enter)");
    gtk_widget_add_css_class(app->btn_login_submit, "btn-primary");
    g_signal_connect(app->btn_login_submit, "clicked", G_CALLBACK(on_auth_submit_clicked), app);
    gtk_box_append(GTK_BOX(card_auth), app->btn_login_submit);

    /* Label lỗi / trạng thái auth */
    app->lbl_auth_error = gtk_label_new("");
    gtk_widget_add_css_class(app->lbl_auth_error, "text-danger");
    gtk_label_set_wrap(GTK_LABEL(app->lbl_auth_error), TRUE);
    gtk_widget_set_halign(app->lbl_auth_error, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card_auth), app->lbl_auth_error);

    app->lbl_conn_status = NULL;

    gtk_box_append(GTK_BOX(center_box), card_auth);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), center_box);
    return scrolled;
}
