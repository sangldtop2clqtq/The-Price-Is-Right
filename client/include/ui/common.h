#ifndef PR_UI_COMMON_H
#define PR_UI_COMMON_H

#include "ui/types.h"

const char *pr_ui_player_name(pr_app *, uint64_t user_id);
GtkWidget *pr_ui_product_image(const char *image_key);

#endif
