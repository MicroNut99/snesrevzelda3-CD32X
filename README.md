# Zelda 3 32X - A Link to the Past on the Sega 32X

A port of [snesrev/zelda3](https://github.com/snesrev/zelda3) (MIT), the C reimplementation of
*The Legend of Zelda: A Link to the Past*, to the **Sega 32X as a cartridge game**, with music from
a Sega CD (CD audio, Sonic32XPlus1's CD code) when one is attached.

**No Nintendo data is in this repository.** Like zelda3 itself, the game's graphics, levels and
music are extracted at build time from your own US ROM (`zelda3.sfc`, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

## Build in one command

With the Sega toolchains in `/opt/toolchains/sega` (or `export GENDEV=/path/to/them`) and the tools
installed once (Ubuntu / WSL: `sudo apt install g++ make python3-pil python3-yaml python3-numpy`),
from this folder, with your ROM's path in place of `/path/to/zelda3.sfc`:

```
cp /path/to/zelda3.sfc zelda3/ && (cd zelda3 && python3 assets/restool.py --extract-from-rom) && mkdir -p build && cp zelda3/zelda3_assets.dat build/ && ./build.sh 2>&1 | tee build_log.txt
```

It writes `Zelda3_32X.32x` and the music CD, `build/Zelda3_32X_CD.cue`. The first build takes a few
minutes (it renders the sound); after that, `./build.sh` alone rebuilds. Everything else - the other
builds and every switch - is under [Building](#building).

## Status: plays in ares - game at full speed in most places, picture ~28 fps where the Genesis planes are used, 10-15 fps elsewhere

The SH-2 runs zelda3's game code at the SNES's 60 Hz and draws as often as it can (frame skip).
With the test build it replays the project's chapter-1 recording and, after every game frame,
compares the SNES RAM - and every drawn picture - with the PC reference (`pc/zhx`).

| Part | State |
|---|---|
| Assets from the ROM | 684 KB (`zelda3_assets.dat`); with ~750 KB of SH-2 code the game fits a 4 MB cartridge |
| Memory | SNES WRAM 128 KB + PPU ~11 KB + VRAM 64 KB + SRAM 8 KB + the Genesis planes' state ~7 KB; heap ~230 KB of the 244 KB below the stacks (master 8 KB, slave 4 KB) - nearly full: a failed allocation stops the game (`Die`) |
| Byte order | the SNES RAM, VRAM and data stay little-endian; all game code is C++ with `le16` / `lei16` / `le32` (`types.h`), so a native pointer into SNES memory is a compile error. Checked on the PC with the 13 chapter recordings (~1.07 million frames, RAM and picture compared every frame) |
| Video: "rip and tear" | the SuperTux REV / Sonic CD method: **BG1 and BG2 are on the Genesis video chip's two planes**, scrolled in hardware; the 32X draws only the sprites and BG3 (status bar, text) over them, each pixel in front of or behind the planes (its through bit). The SH-2 keeps a copy of BG1/BG2 in Genesis video RAM in step with the SNES VRAM (only what changed: ~60 words a frame on average) and the 68000 applies it in the V-blank the 32X shows the frame in (`zelda3/snes/md_planes.c`, `cart/cart_md.s`). The 4 palettes with the most cells on screen get the Genesis's 4 colour lines; the few cells of other palettes are drawn by the 32X. Used whenever the frame allows (no window or mosaic on BG1/BG2, no colour math the Genesis cannot show): 50-91% of frames depending on the chapter (all 13: 72%). Two kinds of colour math are shown exactly: a layer alone on the subscreen under a black backdrop is that layer *under* everything ("flip": the planes swap), and fixed-colour math (darkening, flashes) is a change of the colours ("tint"). Checked on the PC: the Genesis + 32X picture is the same as the SNES picture in every one of those frames (`ZH_MDCHECK`). The 68000 applies the changes by DMA, within one V-blank. Backgrounds have the Genesis's 512 colours (3 bits a channel) |
| Video (other frames) | a software SNES PPU on both SH-2s (master the top lines, slave the rest, balanced every frame). Mode 1 is painted layer by layer in priority order (no depth buffer). Per frame: **8-bit** (CGRAM indices, the CGRAM as the 32X palette with the brightness) or, when the frame has **colour math** (water, fog, rain, transparent floors, dark rooms) or the colour window's clip, **15-bit direct colour** with the subscreen painted too and blended exactly as the SNES does. Mode 7 and mosaic use zelda3's own line renderer |
| Speed (ares) | game logic ~1 ms a frame; Genesis-planes frames ~14 ms to draw (**~29 fps**, a room of chapter 3); 8-bit frames ~43 ms (**~15 fps**), colour-math frames ~120 ms (**~10 fps**, the game slows a little there); frame skip up to 6 |
| Sound effects | every effect the game can send (ports 1-3: effects, the rain / wind ambiences) rendered at build time with zelda3's own SNES sound driver (`pc/zh_sound`), 8-bit at the PWM rate in the cartridge (2.1 MB); the slave SH-2 mixes 5 voices into the 32X PWM in its H interrupt, paced by the PWM's FIFO (`sh2/z32_sound.cpp`). Pitch checked in ares; the rate is one constant (`Z32_PWM_RATE`) if a console plays it off |
| Music | CD audio from a Sega CD under the 32X (Mode 1): `build/Zelda3_32X_CD.cue`, track n = song n (34 tracks, 27 min, one pass each - the drive repeats the songs that loop), rendered with the same driver. The 68000 plays them (`cart/cd.s`, from Sonic32XPlus1); 0xF1-0xF3 fade / lower / restore with the BIOS fader. No Sega CD: no music |
| Controls | Genesis pad: A = Y (item), B = B (sword), C = A (action), X = X (map), Y = L, Z = R, Start = Start, Mode = Select |
| Saves | the game's three files in the cartridge's backup SRAM (8 KB, odd bytes at 0x200001; header `RA`). The ROM's upper half shares that address, so the 68000 switches the SRAM in (0xA130F1) only while it copies it to / from an 8 KB mirror in its RAM, with both SH2s parked in SDRAM (`sh2/z32_save.cpp`, `cart/cart_md.s`); a save sends only what changed. Tested in ares: a file made, the console switched off and on, the file is there |
| Not done | the other colour-math scenes on the Genesis planes (translucent layers, dark rooms, the story's darkened border: an approximation would be needed - the Genesis cannot blend); mode 7 (the world map) at speed; stereo effects (the SNES pans them - mono here) |

### Did porting it to the 32X make the game better?
Honestly: **no - as a way to play A Link to the Past, the SNES original is still better.** The
port is a technical feat and a curiosity, not an improvement. What the 32X version gains and
loses, compared with the SNES game:

Gains
- **Music in CD audio** (with a Sega CD): every song streamed from the disc at 44.1 kHz instead
  of the SNES's sound chip. It is the same music (rendered from the game's own sound driver), so
  it sounds the same - just played back without the SNES's 32 kHz, 64 KB sample limits.
- **No slowdown in the game itself**: the game logic runs at a steady 60 Hz (zelda3's C code on a
  23 MHz SH-2, ~1 ms a frame); only the picture drops frames. The SNES slowed the whole game
  down in busy scenes.
- **Saves on the cartridge** like the original, and zelda3's bug fixes for out-of-range table
  reads (below).
- It runs on a Sega 32X at all - that is the point of the project.

Losses
- **The frame rate**: ~28 fps where the Genesis planes are used, 10-15 fps in scenes with colour
  math (rain, fog, transparent water, dark rooms) and on the world map. The SNES draws 60.
- **Background colours**: BG1/BG2 on the Genesis planes have 3 bits a colour channel (512
  colours) instead of the SNES's 5 - gradients show steps. Sprites and the status bar keep the
  full 15-bit colours.
- **Sound effects in mono** (the SNES pans them) and 8-bit at 15.7 kHz.
- **Music needs a Sega CD** under the 32X; without one, no music.

### Bugs found in zelda3 by the port
Out-of-bounds table reads that the SNES resolves by reading on through the ROM; on the PC they
read whatever the compiler put next (chapter 4 of the recordings differed between builds and
crashed). The tables now carry the ROM's following bytes (marked `Z32X`): `sprite.c`
(`kSprite_PrepAndDrawSingleLarge_Tab1`, sprite 0xEC), `overworld.c` (`kOverworld_OffsetBaseX/Y`,
special areas), `select_file.c` (copy-file fairy and target tables; the SRAM copy wraps as the
SNES's bank $70 mirror). All 13 chapters run clean under AddressSanitizer / UBSan.

### 32X notes
- **The frame buffer drops byte writes of 0** (ares, as hardware): never clear it with `memset`
  (its tail is bytes) - GCC also turns a plain zeroing loop into `memset`, so the PPU's blank
  lines use a `volatile` word loop.
- **Direct colour at 224 lines does not fit the 128 KB frame buffer** (320 x 224 x 2): the lines
  overlap - each starts 576 bytes after the last, 32 black pixels each side, the right border of
  a line is the left border of the next (`sh2/z32_main.cpp`, 129,600 bytes).
- **The SH-2 has 16 registers**: a hot loop inside a big function gets its pointers spilled to
  the stack every pixel - the colour-math loops are small functions of their own (`ppu.c`
  PpuBlendSpan, PpuPalSpan).
- The CRAM takes writes only while PEN (FBCTL bit 13) is set.

## Building

### What you need
- Linux or WSL (the build is a shell script)
- The Sega toolchains: `sh-elf` (GCC with `g++`, for the SH-2s) and `m68k-elf` (`as`, `ld`,
  `objcopy`, for the 68000), in `/opt/toolchains/sega` or wherever `$GENDEV` points
  (e.g. `export GENDEV=/path/to/sega-toolchain-12.1`)
- `python3` with `numpy` (`pip install numpy`; `scipy` too for the best sound resampling),
  plus `Pillow` and `PyYAML` for zelda3's asset tool (`zelda3/requirements.txt`)
- `g++` and `make` (the PC tools in `pc/`)
- Your own US *A Link to the Past* ROM, `zelda3.sfc` (SHA256 `66871d66...009cfb`, above)

### Steps
1. Make the game's assets from your ROM with zelda3's own tool, and copy them to `build/`:
   ```
   cd zelda3
   cp /path/to/zelda3.sfc .
   python3 -m pip install -r requirements.txt   # Pillow, PyYAML
   python3 assets/restool.py --extract-from-rom # writes zelda3_assets.dat
   cd ..
   mkdir -p build && cp zelda3/zelda3_assets.dat build/
   ```
   `restool.py` switches: `-r ROM` (another ROM file name), `--languages de,fr,...` (more
   languages in the .dat - the 32X build uses English only, and the cartridge must stay within
   4 MB), the rest in [zelda3/README.md](zelda3/README.md#building).
2. Build:
   ```
   ./build.sh 2>&1 | tee build_log.txt
   ```
   The first build also renders the sound from the ROM's song banks (a few minutes): the sound
   effects `build/sfx.bin` and the music CD `build/Zelda3_32X_CD.cue` / `.bin`. Delete
   `build/sfx.bin` to make them again. The log ends with `done: .../Zelda3_32X.32x`.
3. Play:
   - `Zelda3_32X.32x` is the cartridge (4 MB, 8 KB of battery SRAM for the saves). Load it in
     ares / Kega Fusion as a 32X game, or put it on a flash cart in a 32X.
   - Music: put `build/Zelda3_32X_CD.cue` in the Sega CD (burn it, or load it as the Sega CD's
     disc in the emulator) with the cartridge in the 32X. Without a Sega CD the game has no music.

### build.sh
`./build.sh [play|replay|test]` - one argument, the kind of cartridge:

| Command | Output | SH-2 switches it sets | What it is |
|---|---|---|---|
| `./build.sh` or `./build.sh play` | `Zelda3_32X.32x` | `-DZ32_NO_CHECK -DZ32_PLAY` | the game: the pad from power-on, saves on |
| `./build.sh replay` | `Zelda3_32X-replay.32x` | `-DZ32_NO_CHECK` | the chapter-1 recording plays itself (at the real speed), then the pad |
| `./build.sh test` | `Zelda3_32X-test.32x` | (none) | the recording, with the SNES RAM and every drawn picture checked against the PC reference (slow: the checks) |

Environment: `GENDEV` - the toolchain folder (default `/opt/toolchains/sega`).

`replay` and `test` need the test data in `build/` (`replay.bin`, `expect.bin`,
`expect_px.bin`); `play` makes empty ones if they are missing:
```
make -C pc -f Makefile.cpp                         # the PC reference, pc/zhx (and pc/zh_sound)
python3 tools/make_testdata.py build/zelda3_assets.dat build [chapter] [frames]
```
`chapter` is 1 (the default; the only recording that starts from power-on - the others start
from a saved state and the script refuses them), `frames` how much of it (default 8000).

### What build.sh does (to run the steps by hand)
1. The sound, once (`build/sfx.bin`, `build/Zelda3_32X_CD.*`):
   ```
   make -C pc -f Makefile.cpp zh_sound
   pc/zh_sound build/zelda3_assets.dat build/snd     # renders every effect and song (.raw)
   python3 tools/make_sound.py build/snd build       # packs sfx.bin, writes the CD image
   ```
2. The 68000 program (`cart/cart_md.s`, which includes `cart/cd.s` and `cart/kos.s`):
   ```
   m68k-elf-as -m68000 --register-prefix-optional --defsym HW_BUILD=1 -o cart_md.o cart_md.s
   m68k-elf-ld -Ttext 0x880800 -o cart_md.elf cart_md.o
   m68k-elf-objcopy -O binary cart_md.elf cart_md.bin
   ```
   `HW_BUILD=1` (always set by build.sh): the Sega CD start-up runs (music). `HW_BUILD=0`
   leaves it out: no Sega CD, no music.
3. The SH-2 program (`sh2/Makefile`, links the assets, the sound and the test data in):
   ```
   make -C sh2 clean
   make -C sh2 -j$(nproc) ROOTDIR=/opt/toolchains/sega Z32_DATA=$PWD/build DEFS="-DZ32_NO_CHECK -DZ32_PLAY"
   ```
   Make variables: `ROOTDIR` (the toolchain folder; default `$GENDEV` or `/opt/toolchains/sega`),
   `Z32_DATA` (the folder with `zelda3_assets.dat`, `sfx.bin`, `replay.bin`, `expect.bin`,
   `expect_px.bin`; default `../build`), `DEFS` (the switches below). Make does not see a change
   of `DEFS`: `make clean` first (build.sh always does).
4. The cartridge: `python3 tools/make_cart.py sh2/z32.bin cart/cart_md.bin Zelda3_32X.32x`
   (the 32X header and boot code from `tools/mars_boot.bin`, the 68000 program, the SH-2 program;
   4 MB with the SRAM header).

### SH-2 switches (`DEFS`)
| Switch | Effect |
|---|---|
| `-DZ32_PLAY` | no recording: the game from power-on with the pad; reads and writes the save file in the cartridge's SRAM |
| `-DZ32_NO_CHECK` | no RAM / picture checks against the PC (the real speed); without it the test checks every frame |
| `-DZ32_NO_MD` | no Genesis planes: every frame drawn whole by the 32X (the "rip and tear" path off) |
| `-DZ32_INDEX8` | every frame drawn in the 8-bit mode (no colour math: wrong colours where the game uses it); also turns the Genesis planes off |
| `-DZ32_DIRECT` | every frame drawn in 15-bit direct colour (slower); also turns the Genesis planes off |
| `-DZ32_NO_SKIP` | draw every game frame (no frame skip: the game slows down to the drawing speed) |
| `-DZ32_STATS_FROM=n` | the timing statistics (`z32_stats` in SDRAM) start at game frame `n` |
| `-DZ32_PPU_TIMING` | FRT ticks per stage of the slow-path drawing, into `g_ppu_t` (test) |
| `-DZ32_HYB_TIMING` | FRT ticks per stage of the Genesis-planes drawing, into `g_hyb_t` (test) |
| `-DZ32_MD_PROF` | FRT ticks per step of the Genesis-planes preparation (`md_planes.c`, test) |
| `-DZ32_SAVE_TEST` | with `Z32_PLAY`: the SRAM self test at power-on (a pattern written and read back; the result in `z32_save_test`: run count << 16, bit 0 last run's pattern found, bit 1 this one read back) |
| `-DZ32_STOP_ON_BAD` | test build: stop at the first frame that differs from the PC and show that picture |
| `-DZ32_PATTERN` | a display test: colour bars, no game |

Set by the build itself, not to be passed: `Z32X` (marks the port's code everywhere; `sh2/Makefile`),
`Z32_MD` (on when the Genesis planes are used: `Z32_NO_CHECK` without `Z32_NO_MD` / `Z32_INDEX8` /
`Z32_DIRECT`). Constants to edit rather than switches: `Z32_PWM_RATE` / `Z32_PWM_CLOCK`
(`sh2/z32_sound.h`, the sound's sample rate - change `PWM_RATE` in `tools/make_sound.py` to the
same value and delete `build/sfx.bin`) and `kMaxSkip` in `sh2/z32_main.cpp` (frame skip, 6).

### The PC tools (`pc/`)
`make -C pc -f Makefile.cpp [target]`:

| Target | What |
|---|---|
| `zhx` (default, with `zh_sound`) | the headless PC reference: zelda3 as the 32X runs it, compiled as C++ |
| `zh_sound` | the sound renderer: `pc/zh_sound <zelda3_assets.dat> <out folder>` |
| `zhx_md` | `zhx` with the Genesis-plane study (`ZH_MDSTAT=file`, `pc/zh_mdstat.c`) |

`pc/Makefile` (`make -C pc` -> `pc/zh`) is the first, C version of the runner; the sources no
longer compile as C, use `Makefile.cpp`.

Run `pc/zhx` from the `zelda3` folder (the recordings are in `zelda3/saves/ref`):
```
cd zelda3
../pc/zhx <zelda3_assets.dat> <frames> [pad script] [frames to save] [out folder]
```
- `pad script`: `frame:bits,frame:bits,...` - the SNES pad bits from that frame on
- `frames to save`: `,f1,f2,...,` - those frames as PPM pictures into `out folder` (default `.`):
  `zNNNNN.ppm` the full colour picture; with `ZH_INDEX8` the 32X's own picture instead
  (`iNNNNN.ppm` 8-bit, `dNNNNN.ppm` direct colour)

Environment switches of `pc/zhx`:

| Variable | Effect |
|---|---|
| `ZH_REPLAY=n` | play recording `n` (0-12 = chapters 1-13) instead of the pad script |
| `ZH_DRAWALL=1` | draw every frame as the game does (needed for hashes the 32X test compares) |
| `ZH_SKIP=n` | draw every n-th frame only (the 32X's frame skip; the RAM must not change) |
| `ZH_HASH=file` | the SNES RAM's hash after every frame |
| `ZH_PIXHASH=file` | the drawn picture's hash per frame |
| `ZH_AUTO=1` | the 32X's picture mode per frame (direct colour only where needed) |
| `ZH_RGB15=1` | always the 15-bit direct colour picture |
| `ZH_INDEX8=1` | save the 32X's picture (see above) |
| `ZH_REF=1` | zelda3's own line renderer only (the reference for the painted one) |
| `ZH_SPLIT=line` | the 32X's two-CPU drawing: lines above `line` in one context, the rest in a copy |
| `ZH_MDCHECK=1` | every frame: the Genesis planes + 32X picture against the whole one; prints the totals and why frames were not on the planes |
| `ZH_MDBAD=dir` | with `ZH_MDCHECK`: the first frames that differ, both pictures as PPM |
| `ZH_MDWHY=1` | with `ZH_MDCHECK`: the first differing pixels of each frame |
| `ZH_MDREG=1` | with `ZH_MDCHECK`: every Genesis register write |
| `ZH_VRAMCHECK=1` | with `ZH_MDCHECK`: every VRAM write was marked for the Genesis's copy |
| `ZH_MATHLOG=1` | with `ZH_MDCHECK`: the colour-math settings of each frame kept off the planes by it |
| `ZH_BG3LOG=frame` | with `ZH_MDCHECK`: the BG3 VRAM marks of 12 frames from `frame` |
| `ZH_MDSTAT=file` | `zhx_md` only: the Genesis-plane study |
| `ZH_MODLOG=1` | the game module / submodule when it changes |
| `ZH_TEXTLOG=1` | with `ZH_MODLOG`: the text system's state in the attract demo (every 20 frames) |
| `ZH_RAMAT=frame` + `ZH_RAMFILE=file` | the SNES RAM after that frame (default `ram.bin`) |
| `ZH_PPUDBG=frame` | the PPU's registers at that frame, and the VRAM + CGRAM into `vram.bin` (from the ROM: not for git) |
| `ZH_APULOG=file` | each new sound command (bank, port, value) |

The full check the port is tested with (all 13 recordings: RAM, picture, Genesis planes):
`ZH_REPLAY=n ZH_MDCHECK=1 ZH_VRAMCHECK=1 ../pc/zhx ../build/zelda3_assets.dat <frames>`.

### Other tools
- `tools/make_music.py <song.vgm> cart/music_stream.bin` (or `--empty cart/music_stream.bin`):
  an FM song for the 68000's player, from a VGM. The cartridge has one already.
- `tools/endian_fix.py`, `tools/le_ptr_fix.py`: the one-off rewrite of zelda3's sources for the
  big-endian SH-2 (already done; not part of the build).

## Layout
- `zelda3/` - snesrev/zelda3 (MIT), with the port's changes (marked `Z32X`); `snes/md_planes.c`:
  BG1 / BG2 on the Genesis planes ("rip and tear")
- `pc/` - the headless PC runner, the reference for the 32X build; `zh_md.c`, the Genesis + 32X
  picture check; `zh_mdstat.c`, the study that sized it; `zh_sound`, the sound renderer
- `sh2/` - the SH-2 program: start-up (`crt0.s`), linker script, the main loop and
  two-CPU drawing (`z32_main.cpp`), sound (`z32_sound.cpp`), saves (`z32_save.cpp`)
- `cart/` - the 68000 program (`cart_md.s`: the Genesis planes' ops, saves,
  pad, CD), CD code
- `tools/` - `make_cart.py` (32X cartridge image), `make_sound.py` (effects bank + music CD),
  `make_testdata.py`, `endian_fix.py` /
  `le_ptr_fix.py` (the byte-order rewrite), `mars_boot.bin`
