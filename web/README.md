# Bingo64 web board generator

A static website that generates the exact board the game makes for a seed.
It is not a reimplementation: the real bingo C code (`src/game/bingo*.c`,
`src/engine/rand.c`) is compiled to WebAssembly with Emscripten, reusing the
`test/host` stub layer. Part A (and half of part B) of
`plans/online-bingo.md`.

What the page does:

- Seed (1–9 digits), game mode (1/2/3 bingos, blackout), and per-objective
  on/off toggles — the same inputs the game's file select screen offers.
- Renders the 5x5 board with the real in-game icons (texture bytes exported
  straight from the compiled data) and in-game titles; hover a cell for the
  full description. Boards are shareable as permalinks (`#s=...&t=...`).
- Exports the 25 goals as Bingosync "Custom (Advanced)" JSON: on
  bingosync.com, Make Room → Game: Custom (Advanced) → paste. (Creating the
  room automatically from this page is not possible: bingosync has no CORS
  API. A goal-list generator PR to bingosync is the plan's optional step 2.)

## Building

Needs the Emscripten SDK (default location `~/opt/emsdk`, override with
`EMSDK=...`) and the generated build inputs (text tables, icon textures).
If you have run the main ROM build once, you already have those inputs.
Without a baserom they can be generated directly — see the "Generate build
prerequisites" step in `.github/workflows/web-check.yml`.

```
make wasm    # build site/dist/bingo64gen.{mjs,wasm}
make serve   # build + serve the site on http://localhost:8064
make check   # byte-compare against test/host goldens + gcc oracle
```

`make check` proves the WASM generator equals the game's code: the 3 golden
boards byte-exact, 200 random seeds, and 60 random option combinations
(target + disabled objectives) against the gcc-built `test/host` oracle in
`BOARD_SEED`/`BOARD_TARGET`/`BOARD_DISABLE` mode. CI runs this on every PR.

## Layout

- `gen/wasm_api.c` — the JS-facing exports. Boards cross the boundary as
  JSON; icons as raw RGBA16 bytes; a dump export replicates the golden-file
  format for `check.mjs`.
- `gen/shims.c` — same fakes as `test/host/glue.c`, but with the real
  course/act name tables (from the generated `define_text.inc.c`) so
  descriptions use real level and star names.
- `site/` — the static page. `site/dist/` (gitignored) holds the built
  module.
- `check.mjs` — the equivalence checker run by `make check`.

## Versioning (future)

The plan calls for one generator version per game release, selectable on the
site. The current page serves a single build (its version, from
`git describe`, is shown in the footer and embedded in every board JSON).
When the next game release happens, move `site/dist` to
`site/dist/<version>/` with a manifest, and add a version picker.
