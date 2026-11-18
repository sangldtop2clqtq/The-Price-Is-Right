#ifndef PR_UI_GAMEPLAY_H
#define PR_UI_GAMEPLAY_H

#include "ui/types.h"

void pr_app_do_game_submit_guess(pr_app *app);
void pr_app_refresh_game_view(pr_app *app);
void pr_app_start_countdown(pr_app *app, uint32_t remaining_ms);
void pr_app_stop_countdown(pr_app *app);
GtkWidget *pr_ui_build_game_page(pr_app *app);

#endif
