CC = cc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Ishared/include
CFLAGS ?= -std=c11 -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=
BUILD ?= build
PYTHON ?= python3
MODULE_MAKEFILES = shared/module.mk server/module.mk client/module.mk
include $(MODULE_MAKEFILES)
PG_PKG_DIR := $(firstword $(wildcard /opt/homebrew/opt/libpq/lib/pkgconfig /usr/local/opt/libpq/lib/pkgconfig))
export PKG_CONFIG_PATH := $(if $(PKG_CONFIG_PATH),$(PKG_CONFIG_PATH):)$(PG_PKG_DIR)
DB_CFLAGS = $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --cflags libpq libsodium)
DB_LIBS = $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config --libs libpq libsodium)

GTK4_CFLAGS = $(shell pkg-config --cflags gtk4)
GTK4_LIBS = $(shell pkg-config --libs gtk4)
GLIB_COMPILE_RESOURCES = glib-compile-resources
GUI_RESOURCES_XML = client/resources/pr.gresource.xml
GUI_RESOURCES_C = $(BUILD)/pr_resources.c

.PHONY: all server cli gui run-gui gui-smoke run-server demo demo-spectator demo-game test unit integration auth-test room-test game-test history-test game-real-test debug ubsan sanitizer-smoke clean check-deps smoke-deps db-start db-stop db-status db-migrate db-seed
all: server cli
server: $(BUILD)/pr_server
cli: $(BUILD)/pr_cli
gui: $(BUILD)/pr_gui

demo:
	$(PYTHON) tools/demo_room.py --players 4 $(DEMO_ARGS)

demo-spectator:
	$(PYTHON) tools/demo_room.py --players 4 --spectators 1 --exercise-fifo $(DEMO_ARGS)

demo-game:
	$(PYTHON) tools/demo_game.py $(DEMO_ARGS)

run-server: server
	mkdir -p .local
	PR_LOG_FILE="$${PR_LOG_FILE:-.local/server.jsonl}" PR_DATABASE_URL="$${PR_DATABASE_URL:-hostaddr=127.0.0.1 port=55432 dbname=price_game user=price_game connect_timeout=5}" ./$(BUILD)/pr_server

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/pr_server: $(SERVER) $(SHARED) $(HEADERS) $(SERVER_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iserver/include $(CFLAGS) $(DB_CFLAGS) $(SERVER) $(SHARED) $(LDFLAGS) $(DB_LIBS) -pthread -lm -o $@

$(BUILD)/pr_cli: tools/cli_client.c $(CLIENT) $(CLIENT_HEADERS) $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) tools/cli_client.c $(CLIENT) $(SHARED) $(LDFLAGS) -o $@

GUI_RESOURCES_O = $(BUILD)/pr_resources.o

GUI_RESOURCES_IMAGES = $(wildcard client/resources/images/*.svg)

$(GUI_RESOURCES_C): $(GUI_RESOURCES_XML) client/resources/style.css $(GUI_RESOURCES_IMAGES) | $(BUILD)
	$(GLIB_COMPILE_RESOURCES) --target=$@ --generate-source --sourcedir=client/resources $<

$(GUI_RESOURCES_O): $(GUI_RESOURCES_C) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK4_CFLAGS) -Wno-overlength-strings -c $< -o $@

$(BUILD)/pr_gui: $(GUI_SRC) $(GUI_RESOURCES_O) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) $(GUI_SRC) $(GUI_RESOURCES_O) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -lm -o $@

run-gui: gui
	./$(BUILD)/pr_gui



$(BUILD)/test_protocol: tests/unit/test_protocol.c $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/unit/test_protocol.c $(SHARED) $(LDFLAGS) -o $@

$(BUILD)/test_auth_logic: tests/unit/test_auth_logic.c server/src/auth/credentials.c server/src/game/scoring.c server/include/pr_credentials.h server/include/pr_scoring.h $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iserver/include $(CFLAGS) tests/unit/test_auth_logic.c server/src/auth/credentials.c server/src/game/scoring.c $(SHARED) $(LDFLAGS) -lm -o $@

$(BUILD)/test_room: tests/unit/test_room.c server/src/game/room.c server/include/pr_room.h $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iserver/include $(CFLAGS) tests/unit/test_room.c server/src/game/room.c $(SHARED) $(LDFLAGS) -o $@

$(BUILD)/test_client: tests/unit/test_client.c $(CLIENT) $(CLIENT_HEADERS) $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) tests/unit/test_client.c $(CLIENT) $(SHARED) $(LDFLAGS) -o $@

$(BUILD)/test_game: tests/unit/test_game.c server/src/game/match.c server/src/game/scoring.c server/include/pr_game.h $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iserver/include $(CFLAGS) tests/unit/test_game.c server/src/game/match.c server/src/game/scoring.c $(SHARED) $(LDFLAGS) -lm -o $@

$(BUILD)/test_account: tests/unit/test_account.c server/src/db/match_json.c server/src/game/match.c server/src/game/scoring.c server/include/pr_persist.h $(SHARED) $(HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iserver/include $(CFLAGS) tests/unit/test_account.c server/src/db/match_json.c server/src/game/match.c server/src/game/scoring.c $(SHARED) $(LDFLAGS) -lm -o $@

$(BUILD)/test_ui_state: tests/unit/test_ui_state.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) tests/unit/test_ui_state.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) -o $@

$(BUILD)/test_ui_net: tests/unit/test_ui_net.c $(GUI_NET_SOURCE) $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_net.c $(GUI_NET_SOURCE) $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_auth_lobby: tests/unit/test_ui_auth_lobby.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_auth_lobby.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_room: tests/unit/test_ui_room.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_room.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_gameplay: tests/unit/test_ui_gameplay.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_gameplay.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_result: tests/unit/test_ui_result.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_result.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_account: tests/unit/test_ui_account.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_account.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_invite: tests/unit/test_ui_invite.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(HEADERS) $(CLIENT_HEADERS) $(GUI_HEADERS) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) -Iclient/include $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_invite.c $(GUI_STATE_SRC) $(CLIENT) $(SHARED) $(LDFLAGS) $(GTK4_LIBS) -o $@

$(BUILD)/test_ui_assets: tests/unit/test_ui_assets.c $(GUI_RESOURCES_O) Makefile $(MODULE_MAKEFILES) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK4_CFLAGS) tests/unit/test_ui_assets.c $(GUI_RESOURCES_O) $(LDFLAGS) $(GTK4_LIBS) -o $@

unit: $(BUILD)/test_protocol $(BUILD)/test_auth_logic $(BUILD)/test_room $(BUILD)/test_client $(BUILD)/test_game $(BUILD)/test_account $(BUILD)/test_ui_state $(BUILD)/test_ui_net $(BUILD)/test_ui_auth_lobby $(BUILD)/test_ui_room $(BUILD)/test_ui_gameplay $(BUILD)/test_ui_result $(BUILD)/test_ui_account $(BUILD)/test_ui_invite $(BUILD)/test_ui_assets
	./$(BUILD)/test_protocol
	./$(BUILD)/test_auth_logic
	./$(BUILD)/test_room
	./$(BUILD)/test_client
	./$(BUILD)/test_game
	./$(BUILD)/test_account
	./$(BUILD)/test_ui_state
	./$(BUILD)/test_ui_net
	./$(BUILD)/test_ui_auth_lobby
	./$(BUILD)/test_ui_room
	./$(BUILD)/test_ui_gameplay
	./$(BUILD)/test_ui_result
	./$(BUILD)/test_ui_account
	./$(BUILD)/test_ui_invite
	./$(BUILD)/test_ui_assets




integration: all
	$(PYTHON) tests/integration/test_transport.py --build-dir $(BUILD)

auth-test: all
	$(PYTHON) tests/integration/test_auth.py --build-dir $(BUILD)

room-test: all
	$(PYTHON) tests/integration/test_room.py --build-dir $(BUILD)

game-test: all
	$(PYTHON) tests/integration/test_game.py --build-dir $(BUILD)

game-real-test: all
	$(PYTHON) tests/integration/test_game.py --build-dir $(BUILD) --real-time

history-test: all
	$(PYTHON) tests/integration/test_history.py --build-dir $(BUILD)

gui-smoke: gui
	$(PYTHON) tests/integration/test_gui_smoke.py

test: unit integration auth-test room-test game-test history-test gui-smoke

db-start:
	$(PYTHON) tools/dev_db.py start

db-stop:
	$(PYTHON) tools/dev_db.py stop

db-status:
	$(PYTHON) tools/dev_db.py status

db-migrate:
	$(PYTHON) tools/dev_db.py migrate

db-seed:
	$(PYTHON) tools/dev_db.py seed

debug:
	$(MAKE) BUILD=build/debug CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' all test

ubsan:
	$(MAKE) BUILD=build/ubsan CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -fno-sanitize-recover=all' LDFLAGS='-fsanitize=undefined' all test

sanitizer-smoke: | $(BUILD)
	$(CC) -std=c11 -fsanitize=address,undefined tools/sanitizer_smoke.c -o $(BUILD)/sanitizer_smoke
	./$(BUILD)/sanitizer_smoke

check-deps:
	@command -v $(CC)
	@command -v $(PYTHON)
	pkg-config --modversion gtk4 libpq libsodium

smoke-deps: | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $$(pkg-config --cflags gtk4 libpq) tools/dependency_smoke.c $$(pkg-config --libs gtk4 libpq) -o $(BUILD)/dependency_smoke
	./$(BUILD)/dependency_smoke

clean:
	$(PYTHON) -c 'import shutil; shutil.rmtree("build", ignore_errors=True)'
