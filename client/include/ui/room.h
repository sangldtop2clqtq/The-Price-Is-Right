#ifndef PR_UI_ROOM_H
#define PR_UI_ROOM_H

#include "ui/types.h"

void pr_app_do_room_ready(pr_app *app);
void pr_app_do_room_start(pr_app *app);
void pr_app_do_room_leave(pr_app *app);
void pr_app_do_room_copy_code(pr_app *app);
void pr_app_refresh_room_view(pr_app *app);
GtkWidget *pr_ui_build_room_page(pr_app *app);

#endif
