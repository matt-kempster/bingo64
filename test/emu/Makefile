# Emulator smoke test for bingo64.
#
# Builds a scripted-input plugin for mupen64plus, boots the ROM headless,
# and checks the screenshots. Needs the mupen64plus install (default
# ~/opt/m64p/install) and its core sources for the plugin API headers.

M64P_SRC ?= $(HOME)/opt/m64p/mupen64plus-core/src/api
BUILD := build

.PHONY: test test-smoke test-ram test-warp test-splat test-starui plugin clean

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

clean:
	rm -rf $(BUILD)
