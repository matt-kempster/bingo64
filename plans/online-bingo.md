# Plan: Online Bingo64

Status: agreed 2026-08-08. Part A is done (see `web/`), with the Bingosync
goal export from part B. Part D is done in its first full version: the move
to the alo code base (2026-08-09), the relay server (TCP + reliable-UDP,
protocol v4), the lobby in file select, the shared board, ghost Mario, and
the live deployment (GCP e2-micro + playit.gg UDP tunnel). See section 9.

Note: this document uses ASD-STE100 (Simplified Technical English) style.

## 1. Purpose

This plan tells you how to put Bingo64 online. The plan has four parts:

- Part A: a website that makes boards.
- Part B: a connection to Bingosync for races.
- Part C: a tool that reads emulator memory. This part is cancelled.
- Part D: a PC port with a shared board and ghost Mario.

## 2. Terms

| Term | Definition |
|---|---|
| The game | The Bingo64 ROM hack in this repository. |
| The generator | The C code that makes a board from a seed. See `src/game/bingo_board_setup.c`. |
| The website | A new web page that makes and shows boards. |
| Bingosync | A public website for bingo races. See bingosync.com. |
| coopdx | The sm64coopdx project. A PC port with 16-player netplay and a Lua mod system. |
| alo | The sm64ex-alo project. A PC port that continues sm64ex. It can also build an N64 ROM. |
| Ghost Mario | A remote player that you can see. You cannot touch a ghost Mario. |
| MVP | The smallest version of a feature that is useful. |

## 3. Decisions

1. Part A: make the website. Compile the C generator to WebAssembly. Do not write the generator again by hand.
2. Part B: use Bingosync for races. Update the connection for each release of the game.
3. Part C: cancelled. Do not make the memory tool.
4. Part D: move the game to the alo code base. Then add the shared board and ghost Mario. Do not use coopdx. Do not write the bingo code in Lua. Do not make a fork that tracks coopdx.

## 4. Part A: the website

### 4.1 Concept

The website makes a board from a seed. The board is equal to the board in the game. Streamers can show the board. Players can examine the board.

### 4.2 Method

Compile the C generator to WebAssembly (WASM) with Emscripten. The generator code is approximately 4,500 lines. It is in `src/game/bingo*.c`, `src/game/splatoon.c`, and `src/engine/rand.c`.

The directory `test/host` compiles the same C code with gcc. It uses stub headers in `test/host/stubs/` and fake functions in `test/host/glue.c`. Use the same stub layer for the Emscripten build. Add a small C function that writes the 25 cells as flat data for JavaScript.

### 4.3 Requirements

- The website must accept a seed of 1 to 9 decimal digits.
- The website must accept the objective on/off options and the bingo target option. These options change the board.
- The website must make the same board as the game for the same inputs.
- CI must compare the WASM output with the golden boards in `test/host/golden/`. CI must also compare the output for many random seeds against the gcc build.
- The website must keep one generator version for each release of the game. A user must be able to select an old version.

### 4.4 Known facts

- The RNG is MT19937. It uses only 32-bit integer math. See `src/engine/rand.c`.
- The generator uses f32 floats in two locations only: the 1-up objective and the special-course coin objective. WASM does f32 operations exactly.
- The cell titles come from `src/game/bingo_titles.c`. They use no RNG. The WASM build gives the exact titles.

## 5. Part B: Bingosync

### 5.1 Concept

Bingosync supplies rooms, player colors, a lockout mode, and chat. We do not build these again.

### 5.2 Method, step 1

The website makes the board. Then the website creates a Bingosync room. It uses the "Custom (Advanced)" board type. This type accepts a JSON list of 25 goals. In this method, Bingosync does not run our code.

### 5.3 Method, step 2 (optional)

Send a Bingo64 generator to the Bingosync project. Bingosync runs each generator as one JavaScript file in Node. A WASM blob in a JavaScript file is possibly not acceptable to them. Ask the Bingosync maintainers before you start this work.

### 5.4 Known facts

- Bingosync is open source: github.com/kbuzsaki/bingosync. The repository has no license file.
- Important API endpoints: `POST /rooms`, `POST /api/join-room`, `PUT /api/select`, `GET /room/<id>/board`.
- The npm package `@gamesdonequick/bingosync-api` can read a board through a websocket. Stream overlays can use it.
- Do not try to make our seeds equal to the Bingosync SM64 seeds. Their generator uses a different algorithm (seedrandom and SRL v5). Our generator is the standard.

### 5.5 Release procedure

For each release of the game:

1. Build the WASM generator from the release source.
2. Put the new generator version on the website.
3. If step 2 of 5.3 is done: send the updated goal list to Bingosync.

## 6. Part C: cancelled — the memory tool

We do not build this tool. Reason: Part D gives a better result for less total work.

If this part becomes necessary later, do this first: add a "beacon" structure to the game. The beacon contains magic bytes, a layout version, the seed, and the 25 cell states. The game writes the beacon each frame. An external tool can find the beacon in emulator memory with one scan. The tool does not need linker-map addresses then. Note: `test/emu/ram_test.py` already shows how to read the board from emulator memory.

## 7. Part D: PC port with ghost Mario

### 7.1 Target

Move the game to the alo code base: github.com/AloUltraExt/sm64ex-alo.

### 7.2 Why alo

- alo continues sm64-port and sm64ex. Those projects are not active. alo is active.
- alo uses decomp Refresh 16.
- alo builds the PC version and the N64 ROM from one repository. We keep the `.z64` releases.
- The port replaces only the hardware layer (video, audio, input). The game logic in `src/game` stays the same. The game plays the same.

### 7.3 Why not coopdx

- coopdx netplay changes almost all actor code. A merge of our tree into coopdx is a large, unnecessary task.
- A fork that tracks coopdx has a permanent merge load. coopdx changes daily.
- A Lua version of the bingo code is a second implementation. Two implementations can become different.

### 7.4 The MVP features

1. Shared board. All players use the same seed. Each game makes the same board locally. The generator is deterministic, so the boards are equal. The server records the cell claims. In lockout mode, the server decides which claim is first.
2. Ghost Mario. Each game sends its state 20 to 30 times each second. The state is: level, area, position, facing angle, animation ID, animation frame. Each game shows the other players as puppets. A puppet has no collision. A puppet has no effect on your game. Interpolate the puppet between updates.
3. Relay server. A small server connects the players. It supplies rooms and NAT traversal. It can also send board data to a web overlay for streams.

Rollback netcode is not necessary. Object synchronization is not necessary. Network errors change smoothness only. They do not change correctness.

### 7.5 The rebase

This is the largest task in the plan. Our base is the decomp of August 2019, near Refresh 1. alo uses Refresh 16. Many function names changed between these versions. Our changes touch the file select screen, the HUD, and many behavior files. The port also touches these areas. Expect merge conflicts there.

These tools decrease the risk:

- `test/host` proves the board logic without an N64.
- The golden boards find unwanted generation changes.
- `test/emu` proves the ROM in an emulator.

Examine these areas first after the rebase:

- Splatoon mode. It writes bits for surface-pool triangles. See `src/game/splatoon.c`.
- The random-star spawner. It uses collision queries. See `src/game/bingo_rando_spawn.c`.

### 7.6 Distribution

Ship binaries without Nintendo assets. The user supplies a ROM at the first start. The program then extracts the assets locally. This is the coopdx model. The source release stays the primary release.

## 8. Sequence

1. Part A: the website with the WASM generator.
2. Part B, step 1: the "create Bingosync room" function. Then, if wanted, step 2.
3. Part D: the rebase to alo.
4. Part D: the relay server, the shared board, then ghost Mario.

Parts A and B do not depend on Part D. Part D does not make parts A and B unnecessary. Emulator and console players continue to use parts A and B.

## 9. Part D progress (2026-08-09)

The `alo-port` branch builds two targets from one tree:

- N64: `make TARGET_N64=1 VERSION=us` makes `build/us/sm64.us.f3dzex.z64`.
  The ROM runs on a stock 4MB console. All emulator tests pass.
- PC: `make VERSION=us` makes `build/us_pc/sm64.us.f3dex2e` (Linux; Windows
  needs winsock work in `src/pc/network/`).

The online components:

- `server/relay.py`: the relay server. Rooms, one shared seed for each
  room, ghost-state relay, and claim arbitration. The default mode is
  co-op (all players share one board). The `--lockout` flag makes the
  first claim of a cell the only claim. Each player has a team number
  for future group battles.
- `src/pc/network/`: the game client. Start the game with
  `--net-server HOST[:PORT] --net-room ROOM --net-name NAME [--net-team N]`.
  The client gets the room seed before file select, so every player
  makes the same board.
- `src/game/bingo_net.c`: ghost Mario puppets (up to 15, animated, no
  collision) and the claim hooks in `set_objective_state`.
- `server/test_relay.py` proves the server (transport + room flows) and
  `test/net/rematch_e2e.py` proves the in-game join/race/back-to-lobby
  loop. The emulator and host tests prove the game still plays the same
  offline. (The original `test/net/protocol_test.py` spoke protocol v1
  and was retired when v5 was current.)

Not done yet: name tags over ghosts, team colors, lockout UI (cells
claimed by others look the same as own claims), Windows sockets, and a
public server.

## 10. References

- Bingosync source: https://github.com/kbuzsaki/bingosync
- Bingosync API client (read only): https://github.com/GamesDoneQuick/bingosync-api
- Example auto-tracking mods: https://github.com/pedroteosousa/HollowKnight.BingoSync, https://github.com/rhelmot/CelesteBingoClient
- OoT bingo generator (versioned generators): https://github.com/ootbingo/oot-bingo-generator
- sm64ex-alo: https://github.com/AloUltraExt/sm64ex-alo
- sm64coopdx: https://github.com/coop-deluxe/sm64coopdx
- coopdx Lua docs: https://github.com/coop-deluxe/sm64coopdx/blob/main/docs/lua/lua.md
- Own-server bingo example: https://github.com/awsker/EldenBingo
