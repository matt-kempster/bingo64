# Web board generator: compiles the real bingo C to WebAssembly.
#
# Same layout as test/host: the game sources are copied into build/ before
# compiling so the stub headers in ../test/host/stubs win over the real game
# headers for "quoted" includes.
#
# Needs:
#  - the Emscripten SDK (default ~/opt/emsdk; override with EMSDK=...)
#  - the main ROM build run at least once (make in the repo root), for
#    text_strings.h, the generated name tables, and the icon texture data.

REPO := ..
BUILD := build
DIST := site/dist

EMSDK ?= $(HOME)/opt/emsdk
EMCC ?= $(EMSDK)/upstream/emscripten/emcc
# emsdk refuses pythons older than 3.10; pick a modern one if present.
export EMSDK_PYTHON ?= $(shell command -v python3.11 || command -v python3.12 || command -v python3)

GEN_VERSION := $(shell git -C $(REPO) describe --always --dirty 2>/dev/null || echo dev)

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

WEB_SRCS := shims.c wasm_api.c

INCLUDES := -I $(REPO)/test/host/stubs -I $(REPO)/src/game -I $(REPO)/src \
	-I $(REPO)/include -I $(REPO) -I $(REPO)/build/us -I $(REPO)/build/us/include
CFLAGS := -O2 -fno-builtin -DVERSION_US -DF3DEX_GBI -DNON_MATCHING -DAVOID_UB \
	-DGEN_VERSION='"$(GEN_VERSION)"' $(INCLUDES)

GAME_OBJS := $(GAME_SRCS:%.c=$(BUILD)/%.o)
WEB_OBJS := $(WEB_SRCS:%.c=$(BUILD)/%.o)

.PHONY: wasm check serve clean

wasm: $(DIST)/bingo64gen.mjs

$(BUILD) $(DIST):
	mkdir -p $@

$(REPO)/build/us/include/text_strings.h:
	$(error Run the main ROM build first (make in the repo root))

$(BUILD)/rand.c: $(REPO)/src/engine/rand.c | $(BUILD)
	cp $< $@

$(BUILD)/%.c: $(REPO)/src/game/%.c | $(BUILD)
	cp $< $@

# Game code compiles with warnings silenced; it is not ours to fix here.
# Recent clang upgrades some 1990s C patterns to errors; downgrade those.
GAME_WARNFLAGS := -w -Wno-error=return-mismatch -Wno-error=incompatible-pointer-types \
	-Wno-error=implicit-function-declaration -Wno-error=implicit-int

$(BUILD)/%.o: $(BUILD)/%.c $(REPO)/build/us/include/text_strings.h
	$(EMCC) $(CFLAGS) $(GAME_WARNFLAGS) -c -o $@ $<

$(BUILD)/%.o: gen/%.c | $(BUILD)
	$(EMCC) $(CFLAGS) -Wall -Wextra -c -o $@ $<

$(DIST)/bingo64gen.mjs: $(GAME_OBJS) $(WEB_OBJS) | $(DIST)
	$(EMCC) -O2 -o $@ $(GAME_OBJS) $(WEB_OBJS) \
		-sMODULARIZE=1 -sEXPORT_ES6=1 \
		-sENVIRONMENT=web,worker,node \
		-sALLOW_MEMORY_GROWTH=1 \
		-sEXPORTED_FUNCTIONS=_gen_board_json,_gen_board_dump,_gen_options_json,_gen_icon_rgba16,_gen_num_objective_types,_gen_version,_malloc,_free \
		-sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8

# Byte-compares the WASM generator against the goldens and against the
# gcc-built oracle in test/host, across many seeds and option combinations.
check: $(DIST)/bingo64gen.mjs
	$(MAKE) -C $(REPO)/test/host build/run_tests
	node check.mjs

serve: wasm
	cd site && python3 -m http.server 8064

clean:
	rm -rf $(BUILD) $(DIST)
