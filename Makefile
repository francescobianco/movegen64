CC     = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Isrc
SRCS   = src/tables.c src/position.c src/movegen.c src/perft.c src/engine.c src/state64.c src/legal64.c src/cost64.c src/relax64.c src/machine64.c src/main.c
LIBSRCS = src/tables.c src/position.c src/movegen.c src/perft.c src/engine.c src/state64.c src/legal64.c src/cost64.c src/relax64.c src/machine64.c
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
	$(CC) $(CFLAGS) $(LIBSRCS) tests/legal64_compare.c -o /tmp/legal64_compare
	/tmp/legal64_compare
	$(CC) $(CFLAGS) $(LIBSRCS) tests/cost64_sim.c -o /tmp/cost64_sim
	/tmp/cost64_sim quick
	$(CC) $(CFLAGS) $(LIBSRCS) tests/machine64_test.c -o /tmp/machine64_test
	/tmp/machine64_test 50

sim:
	$(CC) $(CFLAGS) $(LIBSRCS) tests/cost64_sim.c -o /tmp/cost64_sim
	/tmp/cost64_sim

.PHONY: clean perft check

push:
	@git add .
	@git commit -am "fix"
	@git push