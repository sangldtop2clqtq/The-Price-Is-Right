#include "ui/app.h"
#include <stdio.h>
#include <stdlib.h>

static void on_activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;
    pr_app *app = pr_app_create(gtk_app);
    if (!app || !app->window) {
        fprintf(stderr, "Lỗi: Không thể khởi tạo cửa sổ ứng dụng GTK.\n");
        return;
    }
    gtk_window_present(app->window);
}

int main(int argc, char **argv) {
    /* Đảm bảo mỗi instance chạy độc lập khi mở nhiều terminal test multiplayer */
    GtkApplication *app = gtk_application_new(
        "org.pricegame.desktop",
        G_APPLICATION_NON_UNIQUE
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
