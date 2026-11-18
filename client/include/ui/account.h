#ifndef PR_UI_ACCOUNT_H
#define PR_UI_ACCOUNT_H

#include "ui/types.h"

void pr_app_do_get_stats(pr_app *app);
void pr_app_do_get_history(pr_app *app, uint64_t cursor);
void pr_app_refresh_account_view(pr_app *app);
void pr_app_do_history_prev(pr_app *app);
void pr_app_do_history_next(pr_app *app);
void pr_app_open_account(pr_app *app);
GtkWidget *pr_ui_build_account_page(pr_app *app);

#endif
