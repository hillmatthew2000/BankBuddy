# BankBuddy build
#
#   make              build build/bankbuddy
#   make test         build and run the test suite
#   make run          build and start the app (data in the current directory)
#   make clean        remove all build output
#
# Useful variables:
#   CC=clang          choose the compiler (defaults to gcc)
#   CFLAGS=...        optimisation/debug flags (default: -O2 -g)
#   WERROR=1          treat warnings as errors (used by CI)
#   SANITIZE=1        build with AddressSanitizer + UBSan (gcc/clang on Linux/macOS)

ifeq ($(origin CC),default)
CC := gcc
endif

CFLAGS ?= -O2 -g

WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wvla -Wformat=2 \
            -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
            -Wpointer-arith -Wcast-qual -Wundef

ALL_CPPFLAGS := -Iinclude -MMD -MP $(CPPFLAGS)
ALL_CFLAGS := -std=c11 $(WARNINGS) $(CFLAGS)

ifeq ($(WERROR),1)
ALL_CFLAGS += -Werror
endif

ifeq ($(SANITIZE),1)
ALL_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(OS),Windows_NT)
EXE := .exe
endif

BUILD := build
OBJDIR := $(BUILD)/obj
BIN := $(BUILD)/bankbuddy$(EXE)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(OBJDIR)/%.o)

.PHONY: all run clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
