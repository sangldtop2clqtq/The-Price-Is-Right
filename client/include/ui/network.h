#ifndef PR_UI_NETWORK_H
#define PR_UI_NETWORK_H

#include "ui/types.h"

int  pr_app_connect(pr_app *app, const char *host, uint16_t port);
void pr_app_disconnect(pr_app *app, const char *reason);
void pr_app_ping(pr_app *app);
void pr_app_update_conn_badge(pr_app *app);
void pr_net_source_attach(pr_app *app);
void pr_net_source_detach(pr_app *app);
void pr_net_source_notify_send(pr_app *app);
int pr_ui_dispatch_network(void *context, const pr_header *, const uint8_t *, uint64_t tag);

#endif
