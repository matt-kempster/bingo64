bingo64 (beta)
==============

SM64 Bingo — race your friends to complete bingo objectives.

This download contains NO Nintendo game data. To play, you must provide
your own US Super Mario 64 ROM, dumped from a cartridge you own.

Setup (one time)
----------------
1. Place your ROM in this folder, named:  baserom.us.z64
   (.n64 and .v64 dumps are accepted too; the extractor converts them.)
2. Run:  bingo64-extract.exe baserom.us.z64
   This verifies the ROM is the US version and generates the res/
   folder (textures, skyboxes, music, sound samples) next to the game.
3. Start the game:  sm64.us.f3dex2.exe

If the extractor says the ROM hash doesn't match, your dump is not the
US 8 MB big-endian ROM. Get a clean dump of the US cartridge.

Playing online
--------------
One player (or a server) runs the relay:  python3 relay.py
Everyone enters the relay address in the game's ONLINE menu, picks the
same room name, and readies up on the FILE SELECT screen.

Files
-----
sm64.us.f3dex2.exe     the game (contains no ROM data)
bingo64-extract.exe    asset extractor (run once, needs your ROM)
res/bingo64.custom.zip bingo64's own art and sound structure data
relay.py               online relay server (optional, for hosting)
res/ (generated)       created by the extractor from YOUR rom

Your ROM and the generated res/ folder stay on your machine —
don't redistribute them.
