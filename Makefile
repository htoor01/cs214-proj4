# Makefile for chatd - CS 214 Spring 2026 Project IV
# Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -pthread -g
LDFLAGS = -pthread

TARGET = chatd
TEST_CLIENT = test_client
SOURCES = chatd.c protocol.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = protocol.h

.PHONY: all clean

all: $(TARGET) $(TEST_CLIENT)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TEST_CLIENT): $(TEST_CLIENT).c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) $(TEST_CLIENT) $(OBJECTS)

# Development targets
debug: CFLAGS += -DDEBUG
debug: clean all

test: all
	@echo "Run your test suite here"

.SUFFIXES:
.SUFFIXES: .c .o
