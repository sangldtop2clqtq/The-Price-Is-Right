#include <gtk/gtk.h>
#include <gio/gio.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_gresource_bundle(void) {
    gsize size = 0;
    GError *error = NULL;

    /* 1. Kiểm tra CSS stylesheet */
    gboolean ok = g_resources_get_info("/org/pricegame/style.css",
                                       G_RESOURCE_LOOKUP_FLAGS_NONE,
                                       &size, NULL, &error);
    assert(ok && "style.css must be bundled in GResource");
    assert(size > 100);

    /* 2. Kiểm tra bộ ảnh catalogue và placeholder */
    const char *required_images[] = {
        "/org/pricegame/images/placeholder.svg",
        "/org/pricegame/images/milk.svg",
        "/org/pricegame/images/rice.svg",
        "/org/pricegame/images/coffee.svg",
        "/org/pricegame/images/headphones.svg",
        "/org/pricegame/images/keyboard.svg",
        NULL
    };

    for (int i = 0; required_images[i] != NULL; ++i) {
        size = 0;
        ok = g_resources_get_info(required_images[i],
                                  G_RESOURCE_LOOKUP_FLAGS_NONE,
                                  &size, NULL, NULL);
        assert(ok && "Catalogue asset must be present in GResource");
        assert(size > 50 && "Asset content cannot be empty");

        /* Kiểm tra nội dung không chứa dữ liệu giá bí mật */
        GBytes *bytes = g_resources_lookup_data(required_images[i],
                                               G_RESOURCE_LOOKUP_FLAGS_NONE,
                                               NULL);
        assert(bytes != NULL);
        gconstpointer data = g_bytes_get_data(bytes, &size);
        assert(strstr((const char *)data, "price_vnd") == NULL);
        assert(strstr((const char *)data, "secret") == NULL);
        g_bytes_unref(bytes);
    }

    /* 3. Kiểm tra query tài nguyên không tồn tại trả về FALSE để logic fallback hoạt động */
    ok = g_resources_get_info("/org/pricegame/images/does_not_exist.svg",
                              G_RESOURCE_LOOKUP_FLAGS_NONE,
                              &size, NULL, NULL);
    assert(!ok && "Nonexistent resource must return FALSE");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_gresource_bundle();

    printf("PASS: UI-09 GResource assets, SVG catalogue bundle, fallback integrity and privacy\n");
    return 0;
}
