# Shared wire/transport package: no GTK, PostgreSQL or server dependencies.
SHARED_PROTOCOL_SRC = shared/src/protocol.c shared/src/io.c
SHARED_WIRE_SRC = shared/src/account_wire.c shared/src/game_wire.c shared/src/room_wire.c
SHARED = $(SHARED_PROTOCOL_SRC) $(SHARED_WIRE_SRC)
HEADERS = $(wildcard shared/include/*.h)
