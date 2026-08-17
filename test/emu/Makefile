# Emulator smoke test for bingo64.
#
# Builds a scripted-input plugin for mupen64plus, boots the ROM headless,
# and checks the screenshots. Needs the mupen64plus install (default
# ~/opt/m64p/install) and its core sources for the plugin API headers.

M64P_SRC ?= $(HOME)/opt/m64p/mupen64plus-core/src/api
BUILD := build

.PHONY: test test-smoke test-ram test-warp test-splat test-starui test-diff plugin clean

test: test-smoke test-ram test-warp test-splat test-starui

plugin: $(BUILD)/input_script.so

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/input_script.so: input_script.c | $(BUILD)
	gcc -shared -fPIC -O2 -Wall -Wextra -I $(M64P_SRC) -o $@ $<

test-smoke: plugin
	python3 smoke_test.py

# Needs the core built with DEBUGGER=1 for RAM reads.
test-ram: plugin
	xvfb-run -a python3 ram_test.py

test-warp: plugin
	xvfb-run -a python3 warp_test.py

test-splat: plugin
	xvfb-run -a python3 splatoon_test.py

test-starui: plugin
	xvfb-run -a python3 star_ui_test.py

# Frame-differential: proves ROM B simulates Mario bit-identically to the
# reference ROM A under scripted inputs (the vanilla-by-construction
# gatekeeper — an alo refactor reintegrates only when this passes).
# Runs two corpora: idle-on-castle-grounds, then heavy movement in BOB.
# Not part of `test`: it needs the reference build to exist.
DIFF_ROM_A ?= /home/matt/b64-prealo/build/us/sm64.us.z64
DIFF_MAP_A ?= /home/matt/b64-prealo/build/us/sm64.us.map
DIFF_ROM_B ?= ../../build/us/sm64.us.f3dzex.z64
DIFF_MAP_B ?= ../../build/us/sm64.us.map

test-diff: plugin
	xvfb-run -a python3 diff_test.py \
	    --rom-a $(DIFF_ROM_A) --map-a $(DIFF_MAP_A) \
	    --rom-b $(DIFF_ROM_B) --map-b $(DIFF_MAP_B) \
	    --script scripts/boot_and_board.txt --sync-level 16 --frames 300
	xvfb-run -a python3 diff_test.py \
	    --rom-a $(DIFF_ROM_A) --map-a $(DIFF_MAP_A) \
	    --rom-b $(DIFF_ROM_B) --map-b $(DIFF_MAP_B) \
	    --script scripts/diff_move_bob.txt --warp 9 --sync-level 9 --frames 650

clean:
	rm -rf $(BUILD)
