# Host unit tests for the bingo logic.
#
# This compiles a slice of the real game code with the normal PC compiler,
# so we can run fast checks without a ROM or an emulator.
#
# The game sources are copied into build/ before compiling. This matters:
# gcc looks in the including file's own folder first for "quoted" headers,
# and we need our stub headers in stubs/ to win over the real game headers.
#
# One file (bingo_objective_info.c) includes generated texture data, so the
# main ROM build must have run at least once (make in the repo root).

REPO := ../..
BUILD := build

GAME_SRCS := \
	rand.c \
	strcpy.c \
	bingo.c \
	bingo_board_setup.c \
	bingo_objective_init.c \
	bingo_objective_func.c \
	bingo_objective_info.c \
	bingo_const.c \
	bingo_titles.c \
	bingo_descriptions.c \
	bingo_tracking_collectables.c \
	bingo_tracking_star.c

TEST_SRCS := glue.c test_bingo.c

CC := gcc
INCLUDES := -I stubs -I $(REPO)/src/game -I $(REPO)/src -I $(REPO)/include \
	-I $(REPO) -I $(REPO)/build/us -I $(REPO)/build/us/include
CFLAGS := -g -O1 -fno-builtin -DVERSION_US -DF3DEX_GBI -DNON_MATCHING -DAVOID_UB $(INCLUDES)

GAME_OBJS := $(GAME_SRCS:%.c=$(BUILD)/%.o)
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD)/%.o)

.PHONY: test clean

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests

$(BUILD):
	mkdir -p $(BUILD)

$(REPO)/build/us/include/text_strings.h:
	$(error Run the main ROM build first (make in the repo root))

$(BUILD)/rand.c: $(REPO)/src/engine/rand.c | $(BUILD)
	cp $< $@

$(BUILD)/%.c: $(REPO)/src/game/%.c | $(BUILD)
	cp $< $@

# Game code compiles with warnings silenced; it is not ours to fix here.
$(BUILD)/%.o: $(BUILD)/%.c $(REPO)/build/us/include/text_strings.h
	$(CC) $(CFLAGS) -w -c -o $@ $<

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -Wall -Wextra -c -o $@ $<

$(BUILD)/run_tests: $(GAME_OBJS) $(TEST_OBJS)
	$(CC) -o $@ $^

clean:
	rm -rf $(BUILD)
