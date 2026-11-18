#ifndef PR_UI_APP_H
#define PR_UI_APP_H

#include "ui/types.h"

pr_app *pr_app_create(GtkApplication *gtk_app);
void pr_app_destroy(pr_app *app);
void pr_app_show_screen(pr_app *app, pr_ui_screen_t screen);
void pr_app_show_notification(pr_app *app, const char *msg, const char *level);
void pr_app_refresh_current_view(pr_app *app);

#endif
