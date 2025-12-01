# ==================================================
# TinyShell, developed for:
#
# Operating Systems,
# Department of Electrical and Computer Engineering,
# Aristotle University of Thessaloniki.
# ==================================================

PROJECT := tinyshell

CC := gcc
CFLAGS := -std=c99 -Wpedantic -Wall -Wextra -Os
CFLAGS += -MMD -MP

-include $(OBJS:.o=.d)

SRC_DIR := src
BIN_DIR := bin
OBJ_DIR := obj

TARGET := $(BIN_DIR)/$(PROJECT)

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking: $<"
	@$(CC) -o $@ $^ $(CFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "Compiling: $<"
	@$(CC) -c -o $@ $< $(CFLAGS)

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

clean:
	@rm -rf $(BIN_DIR) $(OBJ_DIR)
	@echo "Clean complete!"

options:
	@echo tinyshell build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "CC       = $(CC)"

run: $(TARGET)
	@$(TARGET)

.PHONY: all clean options run
