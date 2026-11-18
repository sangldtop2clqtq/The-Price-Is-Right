#include "ui/common.h"
#include <stdio.h>
#include <string.h>

const char *pr_ui_player_name(pr_app *app, uint64_t user_id) {
    if (user_id == app->state.self_user_id) {
        return app->state.self_username[0] ? app->state.self_username : "Bạn";
    }
    for (size_t i = 0; i < app->state.game.count; ++i) {
        if (app->state.game.participants[i].user_id == user_id) {
            return app->state.game.participants[i].name;
        }
    }
    for (size_t i = 0; i < app->state.room.player_count; ++i) {
        if (app->state.room.players[i].user_id == user_id) {
            return app->state.room.players[i].name;
        }
    }
    return "Người chơi";
}

GtkWidget *pr_ui_product_image(const char *image_key) {
    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(container, "product-image-container");
    gtk_widget_set_size_request(container, 140, 140);
    gtk_widget_set_halign(container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(container, GTK_ALIGN_CENTER);

    char clean_name[64] = {0};
    if (image_key && image_key[0]) {
        /* Lấy basename bỏ qua folder ví dụ: "products/rice.png" -> "rice.png" */
        const char *base = strrchr(image_key, '/');
        if (base) {
            base++;
        } else {
            base = image_key;
        }
        g_strlcpy(clean_name, base, sizeof(clean_name));
        /* Bỏ phần mở rộng ví dụ: .png, .svg */
        char *dot = strrchr(clean_name, '.');
        if (dot) {
            *dot = '\0';
        }
    }

    char res_path[128];
    gboolean found = FALSE;
    if (clean_name[0]) {
        snprintf(res_path, sizeof(res_path), "/org/pricegame/images/%s.svg", clean_name);
        found = g_resources_get_info(res_path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL);
    }

    if (!found) {
        g_strlcpy(res_path, "/org/pricegame/images/placeholder.svg", sizeof(res_path));
    }

    GdkTexture *texture = gdk_texture_new_from_resource(res_path);
    if (texture) {
        GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
        g_object_unref(texture);
        gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_CONTAIN);
        gtk_widget_set_size_request(pic, 120, 120);
        gtk_widget_add_css_class(pic, "product-image");
        gtk_widget_set_halign(pic, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(pic, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(container), pic);
    } else {
        GtkWidget *lbl = gtk_label_new("📦");
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(lbl, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(container), lbl);
    }

    return container;
}
