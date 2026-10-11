CC = gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=

ifeq ($(OS),Windows_NT)
PROGRAM = Slist.exe
TEST_PROGRAM = test_index.exe
else
PROGRAM = Slist
TEST_PROGRAM = test_index
endif

COMMON_SOURCES = inverted_index.c database_io.c
APP_SOURCES = main.c $(COMMON_SOURCES)
TEST_SOURCES = tests/test_index.c $(COMMON_SOURCES)

.PHONY: all clean test

all: $(PROGRAM)

$(PROGRAM): $(APP_SOURCES) inverted_index.h database_io.h
	$(CC) $(CFLAGS) -I. $(APP_SOURCES) $(LDFLAGS) -o $@

$(TEST_PROGRAM): $(TEST_SOURCES) inverted_index.h database_io.h
	$(CC) $(CFLAGS) -I. $(TEST_SOURCES) $(LDFLAGS) -o $@

test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

clean:
	$(RM) $(PROGRAM) $(TEST_PROGRAM) test_index.db test_index.db.tmp
