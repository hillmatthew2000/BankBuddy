# Simple cross-platform Makefile for BankBuddy

CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wvla -O2
INC = -Iinclude
SRC_DIR = src
OBJ_DIR = build
BIN = bankbuddy

# Sources (exclude gui backends if not present yet)
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Optional flags
ifeq ($(OS),Windows_NT)
  LDLIBS = 
  RM = del /Q
  MKDIR_P = mkdir
else
  LDLIBS = -lm
  RM = rm -f
  MKDIR_P = mkdir -p
endif

.PHONY: all clean run debug

all: $(BIN)

$(BIN): $(OBJ_DIR) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(OBJ_DIR):
	$(MKDIR_P) $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	-$(RM) $(BIN)
	-$(RM) $(OBJ_DIR)/*

# For future GUI flags:
# make GUI=ncurses  (add detection logic later)
