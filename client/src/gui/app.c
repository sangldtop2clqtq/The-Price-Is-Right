#include "ui/app.h"
#include "ui/account.h"
#include "ui/auth.h"
#include "ui/gameplay.h"
#include "ui/lobby.h"
#include "ui/network.h"
#include "ui/results.h"
#include "ui/room.h"
#include <stdio.h>
#include <string.h>

static void apply_css(void) {
    /* Ép buộc dark theme cấp ứng dụng để loại bỏ toàn bộ nền trắng mặc định */
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-interface-color-scheme")) {
            g_object_set(settings, "gtk-interface-color-scheme", 3 /* GTK_INTERFACE_COLOR_SCHEME_FORCE_DARK */, NULL);
        } else if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-application-prefer-dark-theme")) {
            g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
        }
    }

    GtkCssProvider *provider = gtk_css_provider_new();

    /* Nạp từ GResource */
    gtk_css_provider_load_from_resource(provider, "/org/pricegame/style.css");
    
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_USER
        );
    }
    g_object_unref(provider);
}

void pr_app_show_screen(pr_app *app, pr_ui_screen_t screen) {
    if (!app || !app->stack) return;

    /* Bắt buộc người dùng phải đăng nhập thành công trước khi vào sảnh hoặc các màn hình sau */
    if (!app->state.is_authenticated && screen != PR_SCREEN_CONNECT && screen != PR_SCREEN_AUTH) {
        screen = PR_SCREEN_CONNECT;
    }

    const char *page_name = "connect";
    switch (screen) {
        case PR_SCREEN_CONNECT:
        case PR_SCREEN_AUTH: page_name = "connect"; break;
        case PR_SCREEN_LOBBY: page_name = "lobby"; break;
        case PR_SCREEN_ROOM: page_name = "room"; break;
        case PR_SCREEN_GAME: page_name = "game"; break;
        case PR_SCREEN_ROUND_RESULT:
        case PR_SCREEN_FINAL_RESULT: page_name = "result"; break;
        case PR_SCREEN_ACCOUNT: page_name = "account"; break;
    }
    gtk_stack_set_visible_child_name(app->stack, page_name);
    app->state.current_screen = screen;
}

void pr_app_show_notification(pr_app *app, const char *msg, const char *level) {
    (void)level;
    if (!app || !app->lbl_banner || !app->banner_bar) return;
    gtk_label_set_text(GTK_LABEL(app->lbl_banner), msg ? msg : "");
    gtk_widget_set_visible(app->banner_bar, (msg && *msg));
}

pr_app *pr_app_create(GtkApplication *gtk_app) {
    pr_app *app = g_new0(pr_app, 1);
    app->gtk_app = gtk_app;

    /* Nạp CSS */
    apply_css();

    /* Khởi tạo state sạch cho phiên làm việc */
    pr_ui_state_init(&app->state);
    app->state.is_authenticated = false;
    app->state.self_user_id = 0;
    app->state.self_username[0] = '\0';

    /* Cửa sổ chính */
    GtkWidget *win = gtk_application_window_new(gtk_app);
    app->window = GTK_WINDOW(win);
    gtk_window_set_title(app->window, "Đoán Giá Đúng (Price Game) - GTK4 Desktop");
    gtk_window_set_default_size(app->window, 1100, 760);

    /* Root container */
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(app->window, root_box);

    /* 1. Header Bar */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(header, "top-header");

    GtkWidget *title = gtk_label_new("ĐOÁN GIÁ ĐÚNG (PRICE GAME)");
    gtk_widget_add_css_class(title, "app-title");
    gtk_box_append(GTK_BOX(header), title);

    GtkWidget *header_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(header_spacer, TRUE);
    gtk_box_append(GTK_BOX(header), header_spacer);

    GtkWidget *user_badge = gtk_label_new("Chưa đăng nhập");
    gtk_box_append(GTK_BOX(header), user_badge);
    app->lbl_header_user_badge = user_badge;
    app->lbl_header_conn_badge = NULL;

    gtk_box_append(GTK_BOX(root_box), header);

    /* 2. Banner Notification Bar */
    app->banner_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(app->banner_bar, "banner-info");
    app->lbl_banner = gtk_label_new("Chào mừng đến với Price Game! Vui lòng đăng nhập để bắt đầu.");
    gtk_box_append(GTK_BOX(app->banner_bar), app->lbl_banner);
    gtk_box_append(GTK_BOX(root_box), app->banner_bar);

    /* 3. GtkStack chứa các màn hình */
    app->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(app->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(app->stack, 200);
    gtk_widget_set_vexpand(GTK_WIDGET(app->stack), TRUE);

    /* Tạo các pages */
    app->page_connect = pr_ui_build_auth_page(app);
    app->page_lobby = pr_ui_build_lobby_page(app);
    app->page_room = pr_ui_build_room_page(app);
    app->page_game = pr_ui_build_game_page(app);
    app->page_result = pr_ui_build_result_page(app);
    app->page_account = pr_ui_build_account_page(app);

    gtk_stack_add_named(app->stack, app->page_connect, "connect");
    gtk_stack_add_named(app->stack, app->page_lobby, "lobby");
    gtk_stack_add_named(app->stack, app->page_room, "room");
    gtk_stack_add_named(app->stack, app->page_game, "game");
    gtk_stack_add_named(app->stack, app->page_result, "result");
    gtk_stack_add_named(app->stack, app->page_account, "account");

    gtk_box_append(GTK_BOX(root_box), GTK_WIDGET(app->stack));

    /* Cập nhật badge kết nối ban đầu */
    app->state.conn_status = PR_CONN_IDLE;
    pr_app_update_conn_badge(app);

    /* Tự động kết nối ngầm tới máy chủ TCP 127.0.0.1:6666 */
    pr_app_connect(app, "127.0.0.1", 6666);

    /* Mặc định ở màn hình đăng nhập */
    gtk_stack_set_visible_child_name(app->stack, "connect");

    return app;
}

void pr_app_destroy(pr_app *app) {
    if (!app) return;
    pr_app_disconnect(app, NULL);
    if (app->tick_timer_id) {
        g_source_remove(app->tick_timer_id);
        app->tick_timer_id = 0;
    }
    if (app->countdown_timer_id) {
        g_source_remove(app->countdown_timer_id);
        app->countdown_timer_id = 0;
    }
    g_free(app);
}
