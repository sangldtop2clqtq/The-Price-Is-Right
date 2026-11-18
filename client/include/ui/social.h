#ifndef PR_UI_SOCIAL_H
#define PR_UI_SOCIAL_H

#include "ui/types.h"

void pr_app_do_list_players(pr_app *app, uint64_t cursor);
void pr_app_do_invite_player(pr_app *app, uint64_t target_user_id);
void pr_app_refresh_online_players_view(pr_app *app);
void pr_app_refresh_invitations_view(pr_app *app);
void pr_app_accept_invitation(pr_app *app, size_t index);
void pr_app_ignore_invitation(pr_app *app, size_t index);

#endif
