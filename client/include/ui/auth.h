#ifndef PR_UI_AUTH_H
#define PR_UI_AUTH_H

#include "ui/types.h"

void pr_app_do_auth_submit(pr_app *app);
void pr_app_set_auth_mode(pr_app *app, int is_register);
GtkWidget *pr_ui_build_auth_page(pr_app *app);

#endif
