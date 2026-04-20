CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Isrc
SRCS   = src/tables.c src/position.c src/movegen.c src/perft.c src/engine.c src/state64.c src/main.c
LIBSRCS = src/tables.c src/position.c src/movegen.c src/perft.c src/engine.c src/state64.c
TARGET = movegen64
N     ?= 5

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

perft: $(TARGET)
	./$(TARGET) $(N)

clean:
	rm -f $(TARGET)

check: $(TARGET)
	sh tests/perft.sh
	$(CC) $(CFLAGS) $(LIBSRCS) tests/state64_roundtrip.c -o /tmp/state64_roundtrip
	/tmp/state64_roundtrip

.PHONY: clean perft check

push:
	@git add .
	@git commit -am "fix"
	@git push