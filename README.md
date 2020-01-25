# Bingo64

This repo is a fork of the full decompilation of Super Mario 64
that implements the popular speedrunning challenge known as bingo in
the game itself.

To play the game, optionally enter a seed in the seeds menu during
file selection, then proceed to any file. Press L to view the generated
bingo board, and use the D-pad to get more information about a particular
objective. You win if you complete 5 objectives in a row.

This repo does not include all assets necessary for compiling the ROMs.
A prior copy of the game is required to extract the required assets.
The remainder of this README will reflect the decompiled source code's
installation instructions, as they are quite involved.

## Installation

### Linux

#### 1. Copy baserom(s) for asset extraction

For each version (jp/us/eu) that you want to build a ROM for, put an existing ROM at
`./baserom.<version>.z64` for asset extraction.

#### 2. Install build dependencies

The build system has the following package requirements:
 * binutils-mips >= 2.27
 * python3 >= 3.6
 * libaudiofile
 * qemu-irix

__Debian / Ubuntu__
```
sudo apt install build-essential pkg-config git binutils-mips-linux-gnu python3 zlib1g-dev libaudiofile-dev
```

Download latest package from [qemu-irix Releases](https://github.com/n64decomp/qemu-irix/releases)
```
sudo dpkg -i qemu-irix-2.11.0-2169-g32ab296eef_amd64.deb
```

(Optional) Clone https://github.com/n64decomp/qemu-irix and follow the install instructions in the README.

__Arch Linux__
```
sudo pacman -S base-devel python audiofile
```
Install the following AUR packages:
* [mips64-elf-binutils](https://aur.archlinux.org/packages/mips64-elf-binutils) (AUR)
* [qemu-irix-git](https://aur.archlinux.org/packages/qemu-irix-git) (AUR)

#### 3. Build ROM

Run `make` to build the ROM (defaults to `VERSION=us`). Make sure your path to the repo
is not too long or else this process will error, as the emulated IDO compiler cannot
handle paths longer than 255 characters.
Examples:
```
make VERSION=jp -j4       # build (J) version instead with 4 jobs
make VERSION=eu COMPARE=0 # non-matching EU version still WIP
```

## Windows

For Windows, install WSL and a distro of your choice following
[Windows Subsystem for Linux Installation Guide for Windows 10](https://docs.microsoft.com/en-us/windows/wsl/install-win10)
We recommend either Debian or Ubuntu 18.04 Linux distributions under WSL.

Then follow the directions in the [Linux](#linux) installation section above.

## macOS

macOS is currently unsupported as qemu-irix is unable to be built for macOS host.
The recommended path is installing a Linux distribution under a VM.

## Project Structure

```
sm64
├── actors: object behaviors, geo layout, and display lists
├── asm: handwritten assembly code, rom header
│   └── non_matchings: asm for non-matching sections
├── assets: animation and demo data
│   ├── anims: animation data
│   └── demos: demo data
├── bin: asm files for ordering display lists and textures
├── build: output directory
├── data: behavior scripts, misc. data
├── doxygen: documentation infrastructure
├── enhancements: example source modifications
├── include: header files
├── levels: level scripts, geo layout, and display lists
├── lib: SDK library code
├── sound: sequences, sound samples, and sound banks
├── src: C source code for game
│   ├── audio: audio code
│   ├── buffers: stacks, heaps, and task buffers
│   ├── engine: script processing engines and utils
│   ├── game: behaviors and rest of game source
│   ├── goddard: Mario intro screen
│   └── menu: title screen and file, act, and debug level selection menus
├── text: dialog, level names, act names
├── textures: skybox and generic texture data
└── tools: build tools
```

## Contributing

Pull requests are welcome. For major changes, please open an issue first to
discuss what you would like to change. Some instructions have been placed in
bingo_guide.txt, if you would like to add a new objective type.

Run clang-format on your code to ensure it meets the project's coding standards.

Official discord: https://discord.gg/27JtCWs