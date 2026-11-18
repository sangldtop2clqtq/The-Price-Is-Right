#include "ui/network.h"
#include "ui/app.h"
#include <stdio.h>
#include <string.h>

void pr_app_update_conn_badge(pr_app *app) {
    if (!app || !app->lbl_header_conn_badge) return;

    gtk_widget_remove_css_class(app->lbl_header_conn_badge, "badge-connected");
    gtk_widget_remove_css_class(app->lbl_header_conn_badge, "badge-connecting");
    gtk_widget_remove_css_class(app->lbl_header_conn_badge, "badge-disconnected");
    gtk_widget_remove_css_class(app->lbl_header_conn_badge, "badge-idle");

    char text[128];
    switch (app->state.conn_status) {
        case PR_CONN_CONNECTED:
            snprintf(text, sizeof(text), "Đã kết nối (%s:%u)", app->state.server_ip, app->state.server_port);
            gtk_widget_add_css_class(app->lbl_header_conn_badge, "badge-connected");
            if (app->btn_connect) gtk_button_set_label(GTK_BUTTON(app->btn_connect), "Ngắt kết nối");
            if (app->btn_ping) gtk_widget_set_sensitive(app->btn_ping, TRUE);
            break;
        case PR_CONN_CONNECTING:
            snprintf(text, sizeof(text), "Đang kết nối (%s:%u)...", app->state.server_ip, app->state.server_port);
            gtk_widget_add_css_class(app->lbl_header_conn_badge, "badge-connecting");
            if (app->btn_connect) gtk_button_set_label(GTK_BUTTON(app->btn_connect), "Hủy kết nối");
            if (app->btn_ping) gtk_widget_set_sensitive(app->btn_ping, FALSE);
            break;
        case PR_CONN_ERROR:
            snprintf(text, sizeof(text), "Lỗi kết nối (%s:%u)", app->state.server_ip, app->state.server_port);
            gtk_widget_add_css_class(app->lbl_header_conn_badge, "badge-disconnected");
            if (app->btn_connect) gtk_button_set_label(GTK_BUTTON(app->btn_connect), "Thử kết nối lại");
            if (app->btn_ping) gtk_widget_set_sensitive(app->btn_ping, FALSE);
            break;
        case PR_CONN_IDLE:
        default:
            snprintf(text, sizeof(text), "Chưa kết nối");
            gtk_widget_add_css_class(app->lbl_header_conn_badge, "badge-idle");
            if (app->btn_connect) gtk_button_set_label(GTK_BUTTON(app->btn_connect), "Kết nối");
            if (app->btn_ping) gtk_widget_set_sensitive(app->btn_ping, FALSE);
            break;
    }
    gtk_label_set_text(GTK_LABEL(app->lbl_header_conn_badge), text);
    if (app->lbl_conn_status) {
        gtk_label_set_text(GTK_LABEL(app->lbl_conn_status), text);
    }
}

int pr_app_connect(pr_app *app, const char *host, uint16_t port) {
    if (!app) return -1;
    if (app->state.net.fd >= 0) {
        pr_app_disconnect(app, "Ngắt kết nối cũ để kết nối lại");
    }

    snprintf(app->state.server_ip, sizeof(app->state.server_ip), "%s", host);
    app->state.server_port = port;

    pr_connection_init(&app->state.net, pr_ui_dispatch_network, app);
    double now = pr_client_now();

    int res = pr_connection_open(&app->state.net, host, port, now);
    if (res < 0) {
        app->state.conn_status = PR_CONN_ERROR;
        pr_app_update_conn_badge(app);
        char msg[256];
        snprintf(msg, sizeof(msg), "Không thể mở socket tới %s:%u (%s)",
                 host, port, app->state.net.error ? app->state.net.error : "Lỗi socket");
        pr_app_show_notification(app, msg, "error");
        return -1;
    }

    if (app->state.net.connecting) {
        app->state.conn_status = PR_CONN_CONNECTING;
    } else {
        app->state.conn_status = PR_CONN_CONNECTED;
    }

    pr_app_update_conn_badge(app);
    pr_net_source_attach(app);
    return 0;
}

void pr_app_disconnect(pr_app *app, const char *reason) {
    if (!app) return;
    pr_net_source_detach(app);
    if (app->state.net.fd >= 0) {
        pr_connection_close(&app->state.net);
    }
    app->state.conn_status = PR_CONN_IDLE;
    pr_app_update_conn_badge(app);

    if (reason && *reason) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Đã ngắt kết nối: %s", reason);
        pr_app_show_notification(app, msg, "error");
    } else {
        pr_app_show_notification(app, "Đã ngắt kết nối với máy chủ TCP.", "info");
    }
}

void pr_app_ping(pr_app *app) {
    if (!app || app->state.net.fd < 0 || app->state.conn_status != PR_CONN_CONNECTED) {
        pr_app_show_notification(app, "Chưa kết nối tới máy chủ, không thể gửi PING.", "error");
        return;
    }
    static uint32_t ping_counter = 1000;
    uint32_t nonce = ++ping_counter;
    if (pr_command_ping(&app->state.net, nonce) < 0) {
        pr_app_show_notification(app, "Gửi PING thất bại (hàng đợi TX đầy hoặc socket lỗi).", "error");
        return;
    }
    pr_net_source_notify_send(app);
    char msg[128];
    snprintf(msg, sizeof(msg), "Đang gửi PING tới máy chủ (Nonce: %u)...", nonce);
    pr_app_show_notification(app, msg, "info");
}
