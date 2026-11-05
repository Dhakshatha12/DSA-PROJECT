# PathFinder-C - build with:  make        run with:  make run
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
SRC      = src/strbuf.c src/graph.c src/ds.c src/algorithms.c src/server.c src/main.c
CORE     = src/strbuf.c src/graph.c src/ds.c src/algorithms.c

ifeq ($(OS),Windows_NT)
  BIN  = pathfinder.exe
  TEST = test_algorithms.exe
  LIBS = -lws2_32 -lshell32
else
  BIN  = pathfinder
  TEST = test_algorithms
  LIBS =
endif

all: $(BIN)

$(BIN): $(SRC) src/*.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

$(TEST): tests/test_algorithms.c $(CORE) src/*.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_algorithms.c $(CORE)

run: $(BIN)
	./$(BIN)

test: $(TEST)
	./$(TEST)

clean:
	rm -f pathfinder pathfinder.exe test_algorithms test_algorithms.exe

.PHONY: all run test clean
