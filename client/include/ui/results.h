#ifndef PR_UI_RESULTS_H
#define PR_UI_RESULTS_H

#include "ui/types.h"

void pr_app_refresh_result_view(pr_app *app);
void pr_app_do_result_return_room(pr_app *app);
void pr_app_do_result_return_lobby(pr_app *app);
GtkWidget *pr_ui_build_result_page(pr_app *app);

#endif
