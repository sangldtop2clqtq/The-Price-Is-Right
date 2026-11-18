# Server packages. The executable composes auth, persistence, game and handlers.
SERVER_AUTH_SRC = server/src/auth/auth.c server/src/auth/credentials.c server/src/auth/hash_worker.c
SERVER_DB_SRC = server/src/db/async.c server/src/db/match_json.c server/src/db/persist.c
SERVER_GAME_SRC = server/src/game/match.c server/src/game/engine.c server/src/game/scoring.c server/src/game/room.c
SERVER_HANDLER_SRC = server/src/handlers/account_handler.c server/src/handlers/room_handler.c
SERVER = server/src/main.c server/src/log.c $(SERVER_AUTH_SRC) $(SERVER_DB_SRC) $(SERVER_GAME_SRC) $(SERVER_HANDLER_SRC)
SERVER_HEADERS = $(wildcard server/include/*.h)
