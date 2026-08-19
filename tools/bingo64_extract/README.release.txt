bingo64 (playtest)
==================

SM64 Bingo -- race your friends to complete bingo goals.

This download contains NO Nintendo game data. You need your own US
Super Mario 64 ROM, dumped from a cartridge you own.

Setup (one time, about a minute)
--------------------------------
1. Put your ROM in this folder, named:  baserom.us.z64
   (.n64 / .v64 dumps are fine too; the extractor converts them.)
2. Double-click:  bingo64-extract.exe   (it finds the ROM by itself)
3. Start the game:  sm64.us.f3dex2e.exe

If the extractor rejects your ROM, it isn't the US 8MB version --
get a clean dump of a US cartridge.

Playing online
--------------
The game finds the server by itself. In the game:

  ONLINE door -> pick a NAME and COLOR, type the ROOM name your
  group agreed on -> CONNECT -> READY.

Everyone who types the same room name is in the same race. The host
(first one in) presses START RACE. Mid-race, pause + R opens the
online menu (LEAVE RACE; the host can send everyone BACK TO LOBBY).

Good to know
------------
- Playtest updates are frequent and sometimes break old versions.
  If the game says "update needed", download the newest zip.
- Your ROM and the generated res/ folder stay on your machine.
  The zip is shareable; your ROM is not.
- Settings live in %APPDATA%\sm64ex\sm64config.txt -- only edit it
  while the game is closed (the game rewrites it on exit).

Files
-----
sm64.us.f3dex2e.exe     the game (contains no ROM data)
bingo64-extract.exe     one-time extractor (needs your ROM)
res/bingo64.custom.zip  bingo64's own art + sound structure
res/ (generated)        created from YOUR rom by the extractor
relay.py                server code, for self-hosting (optional)
