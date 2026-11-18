#ifndef PR_UI_LOBBY_H
#define PR_UI_LOBBY_H

#include "ui/types.h"

void pr_app_do_logout(pr_app *app);
void pr_app_do_create_room(pr_app *app);
void pr_app_do_join_room(pr_app *app, const char *code);
GtkWidget *pr_ui_build_lobby_page(pr_app *app);

#endif
