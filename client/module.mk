# Core is shared by CLI and GTK; only GUI packages depend on GTK.
CLIENT = client/src/core/connection.c client/src/core/commands.c
CLIENT_HEADERS = client/include/pr_client.h

GUI_APP_SRC = client/src/gui/main.c client/src/gui/app.c
GUI_STATE_SRC = client/src/gui/model/state.c
GUI_NET_SOURCE = client/src/gui/network/net_source.c
GUI_NETWORK_SRC = $(GUI_NET_SOURCE) client/src/gui/network/controller.c client/src/gui/network/events.c
GUI_COMMON_SRC = client/src/gui/common/presentation.c
GUI_AUTH_SRC = client/src/gui/features/auth/auth.c
GUI_LOBBY_SRC = client/src/gui/features/lobby/lobby.c
GUI_ROOM_SRC = client/src/gui/features/room/room.c
GUI_GAMEPLAY_SRC = client/src/gui/features/gameplay/gameplay.c
GUI_RESULTS_SRC = client/src/gui/features/results/results.c
GUI_ACCOUNT_SRC = client/src/gui/features/account/account.c
GUI_SOCIAL_SRC = client/src/gui/features/social/social.c
GUI_SRC = $(GUI_APP_SRC) $(GUI_STATE_SRC) $(GUI_NETWORK_SRC) $(GUI_COMMON_SRC) \
          $(GUI_AUTH_SRC) $(GUI_LOBBY_SRC) $(GUI_ROOM_SRC) $(GUI_GAMEPLAY_SRC) \
          $(GUI_RESULTS_SRC) $(GUI_ACCOUNT_SRC) $(GUI_SOCIAL_SRC)
GUI_HEADERS = client/include/pr_ui.h client/include/pr_ui_state.h $(wildcard client/include/ui/*.h)
