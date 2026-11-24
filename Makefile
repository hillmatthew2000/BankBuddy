# BankBuddy build
#
#   make              build build/bankbuddy
#   make test         build and run the test suite
#   make run          build and start the app (data in the current directory)
#   make format       reformat the sources with clang-format
#   make format-check fail if any source is not formatted (used by CI)
#   make clean        remove all build output
#
# Useful variables:
#   CC=clang          choose the compiler (defaults to gcc)
#   CFLAGS=...        optimisation/debug flags (default: -O2 -g)
#   WERROR=1          treat warnings as errors (used by CI)
#   SANITIZE=1        build with AddressSanitizer + UBSan (gcc/clang on Linux/macOS)
#   CLANG_FORMAT=...  the clang-format to use (CI pins version 18)

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

TEST_BIN := $(BUILD)/run_tests$(EXE)
TEST_DIR := $(BUILD)/test-data

CLANG_FORMAT ?= clang-format
FORMAT_FILES := $(wildcard src/*.c include/*.h tests/*.c tests/*.h)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=$(OBJDIR)/%.o)
LIB_OBJS := $(filter-out $(OBJDIR)/main.o,$(OBJS))
TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(OBJDIR)/tests/%.o)

.PHONY: all test run format format-check clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(TEST_BIN): $(TEST_OBJS) $(LIB_OBJS)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# The tests create and delete data files, so they get a directory of their own.
test: $(TEST_BIN)
	mkdir -p $(TEST_DIR)
	BANKBUDDY_TEST_DIR=$(TEST_DIR) ./$(TEST_BIN)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR)/tests/%.o: tests/%.c | $(OBJDIR)/tests
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -c $< -o $@

$(OBJDIR) $(OBJDIR)/tests:
	mkdir -p $@

run: $(BIN)
	./$(BIN)

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)
