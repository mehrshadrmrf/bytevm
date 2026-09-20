CC       ?= cc
STD      := -std=c17
WARN     := -Wall -Wextra -Wpedantic -Werror
DBG      := -g -O0
INCLUDE  := -Iinclude -Isrc

BUILD    := build
BIN      := $(BUILD)/bytevm

CORE_SRC := $(wildcard src/*.c) $(wildcard src/assembler/*.c)
CORE_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))

TEST_SRC := tests/test_main.c $(wildcard tests/unit/*.c)
TEST_BIN := $(BUILD)/run_tests

.PHONY: all build test clean format

all: build

build: $(BIN)

$(BIN): $(CORE_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(DBG) $^ -o $@

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(DBG) $(INCLUDE) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(filter-out src/main.c,$(CORE_SRC))
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(DBG) $(INCLUDE) $^ -o $@

FORMAT_FILES := $(wildcard include/bytevm/*.h) $(CORE_SRC) \
                 $(wildcard src/assembler/*.h) $(wildcard tests/*.c) $(wildcard tests/unit/*.c)

format:
	clang-format -i $(FORMAT_FILES)

clean:
	rm -rf $(BUILD)
