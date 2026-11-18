#include "ui/social.h"
#include "ui/app.h"
#include "ui/lobby.h"
#include "ui/network.h"
#include <stdio.h>
#include <string.h>

static void on_send_invite_clicked(GtkButton *btn, gpointer user_data) {
    pr_app *app = (pr_app *)user_data;
    uint64_t target = (uint64_t)(uintptr_t)g_object_get_data(G_OBJECT(btn), "target_user_id");
    if (app && target) {
        pr_app_do_invite_player(app, target);
    }
}

static void on_accept_invite_clicked(GtkButton *btn, gpointer user_data) {
    pr_app *app = (pr_app *)user_data;
    size_t index = (size_t)(uintptr_t)g_object_get_data(G_OBJECT(btn), "invite_slot");
    if (app) {
        pr_app_accept_invitation(app, index);
    }
}

static void on_ignore_invite_clicked(GtkButton *btn, gpointer user_data) {
    pr_app *app = (pr_app *)user_data;
    size_t index = (size_t)(uintptr_t)g_object_get_data(G_OBJECT(btn), "invite_slot");
    if (app) {
        pr_app_ignore_invitation(app, index);
    }
}

void pr_app_do_list_players(pr_app *app, uint64_t cursor) {
    if (!app || app->state.net.fd < 0 || !app->state.is_authenticated) return;
    if (pr_command_page(&app->state.net, PR_LIST_PLAYERS, cursor, 20) == 0) {
        pr_net_source_notify_send(app);
    }
}

void pr_app_do_invite_player(pr_app *app, uint64_t target_user_id) {
    if (!app || app->state.net.fd < 0 || !app->state.is_authenticated || !target_user_id) return;
    if (pr_command_invite(&app->state.net, target_user_id) == 0) {
        pr_net_source_notify_send(app);
        char msg[128];
        snprintf(msg, sizeof(msg), "Đang gửi lời mời thi đấu tới người chơi #%llu...", (unsigned long long)target_user_id);
        pr_app_show_notification(app, msg, "info");
    } else {
        pr_app_show_notification(app, "Không thể gửi lời mời thi đấu.", "error");
    }
}

void pr_app_refresh_online_players_view(pr_app *app) {
    if (!app || !app->box_online_players) return;

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->box_online_players)) != NULL) {
        gtk_box_remove(GTK_BOX(app->box_online_players), child);
    }

    if (app->state.online_count == 0) {
        GtkWidget *lbl_none = gtk_label_new("Không có người chơi nào online ngoài phòng.");
        gtk_widget_add_css_class(lbl_none, "text-muted");
        gtk_box_append(GTK_BOX(app->box_online_players), lbl_none);
        return;
    }

    for (size_t i = 0; i < app->state.online_count && i < 20; ++i) {
        pr_online_view *p = &app->state.online_players[i];
        if (p->user_id == 0 || p->user_id == app->state.self_user_id) continue;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "table-row");

        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "%s (#%llu)", p->name, (unsigned long long)p->user_id);
        GtkWidget *lbl = gtk_label_new(name_buf);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        GtkWidget *btn_inv = gtk_button_new_with_label("Mời vào phòng");
        gtk_widget_add_css_class(btn_inv, "btn-primary");
        g_object_set_data(G_OBJECT(btn_inv), "target_user_id", (gpointer)(uintptr_t)p->user_id);
        g_signal_connect(btn_inv, "clicked", G_CALLBACK(on_send_invite_clicked), app);

        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), btn_inv);
        gtk_box_append(GTK_BOX(app->box_online_players), row);
    }
}

void pr_app_refresh_invitations_view(pr_app *app) {
    if (!app || !app->box_lobby_invites) return;

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->box_lobby_invites)) != NULL) {
        gtk_box_remove(GTK_BOX(app->box_lobby_invites), child);
    }

    double now = pr_client_now();
    size_t active_count = 0;

    for (size_t i = 0; i < 16; ++i) {
        pr_ui_invitation *inv = &app->state.invitations[i];
        if (!inv->active) continue;

        if (inv->deadline <= now) {
            inv->active = false;
            continue;
        }

        active_count++;
        uint32_t rem_sec = (uint32_t)(inv->deadline - now);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "table-row");

        char text[160];
        snprintf(text, sizeof(text), "Người chơi #%llu mời bạn vào phòng [%s] (còn %us)",
                 (unsigned long long)inv->view.inviter, inv->view.code, rem_sec);
        GtkWidget *lbl = gtk_label_new(text);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        GtkWidget *btn_acc = gtk_button_new_with_label("Chấp nhận");
        gtk_widget_add_css_class(btn_acc, "btn-primary");
        g_object_set_data(G_OBJECT(btn_acc), "invite_slot", (gpointer)(uintptr_t)i);
        g_signal_connect(btn_acc, "clicked", G_CALLBACK(on_accept_invite_clicked), app);

        GtkWidget *btn_ign = gtk_button_new_with_label("Bỏ qua");
        gtk_widget_add_css_class(btn_ign, "btn-secondary");
        g_object_set_data(G_OBJECT(btn_ign), "invite_slot", (gpointer)(uintptr_t)i);
        g_signal_connect(btn_ign, "clicked", G_CALLBACK(on_ignore_invite_clicked), app);

        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), btn_acc);
        gtk_box_append(GTK_BOX(row), btn_ign);
        gtk_box_append(GTK_BOX(app->box_lobby_invites), row);
    }

    if (active_count == 0) {
        GtkWidget *lbl_none = gtk_label_new("Chưa có lời mời thi đấu nào mới.");
        gtk_widget_add_css_class(lbl_none, "text-muted");
        gtk_widget_set_halign(lbl_none, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(app->box_lobby_invites), lbl_none);
    }
}

void pr_app_accept_invitation(pr_app *app, size_t index) {
    if (!app || index >= 16 || !app->state.invitations[index].active) return;
    char code[8];
    snprintf(code, sizeof(code), "%s", app->state.invitations[index].view.code);
    app->state.invitations[index].active = false;
    pr_app_refresh_invitations_view(app);
    pr_app_do_join_room(app, code);
}

void pr_app_ignore_invitation(pr_app *app, size_t index) {
    if (!app || index >= 16) return;
    app->state.invitations[index].active = false;
    pr_app_refresh_invitations_view(app);
    pr_app_show_notification(app, "Đã bỏ qua lời mời thi đấu.", "info");
}
