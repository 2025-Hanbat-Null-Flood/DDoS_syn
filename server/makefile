# Build config
CC      ?= gcc
CSTD    ?= -std=c11
WARN    ?= -Wall -Wextra -Wpedantic
OPT     ?= -O2
CFLAGS  ?= $(CSTD) $(WARN) $(OPT)
LDFLAGS ?=

# Sources
SRCS := server_main.c server_control.c server_print.c
OBJS := $(SRCS:.c=.o)
DEPS := server.h

# Default
all: server

server: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build (ASan+UBSan)
debug: CFLAGS := $(CSTD) $(WARN) -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean server

run: server
	./server

clean:
	$(RM) $(OBJS) server

.PHONY: all debug run clean
