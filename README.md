# Zelda 3 32X - A Link to the Past on the Sega 32X

A port of [snesrev/zelda3](https://github.com/snesrev/zelda3) (MIT), the C reimplementation of
*The Legend of Zelda: A Link to the Past*, to the **Sega 32X as a cartridge game**, with music from
a Sega CD (CD audio, Sonic32XPlus1's CD code) when one is attached.

**No Nintendo data is in this repository.** Like zelda3 itself, the game's graphics, levels and
music are extracted at build time from your own US ROM (`zelda3.sfc`, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

## Status: plays in ares - game at full speed in most places, picture ~28 fps where the Genesis planes are used, 10-15 fps elsewhere

The SH-2 runs zelda3's game code at the SNES's 60 Hz and draws as often as it can (frame skip).
With the test build it replays the project's chapter-1 recording and, after every game frame,
compares the SNES RAM - and every drawn picture - with the PC reference (`pc/zhx`).

| Part | State |
|---|---|
| Assets from the ROM | 684 KB (`zelda3_assets.dat`); with ~750 KB of SH-2 code the game fits a 4 MB cartridge |
| Memory | SNES WRAM 128 KB + PPU ~11 KB + VRAM 64 KB + SRAM 8 KB + the Genesis planes' state ~7 KB; heap ~230 KB of the 244 KB below the stacks (master 8 KB, slave 4 KB) - nearly full: a failed allocation stops the game (`Die`) |
| Byte order | the SNES RAM, VRAM and data stay little-endian; all game code is C++ with `le16` / `lei16` / `le32` (`types.h`), so a native pointer into SNES memory is a compile error. Checked on the PC with the 13 chapter recordings (~1.07 million frames, RAM and picture compared every frame) |
| Video: "rip and tear" | the SuperTux REV / Sonic CD method: **BG1 and BG2 are on the Genesis video chip's two planes**, scrolled in hardware; the 32X draws only the sprites and BG3 (status bar, text) over them, each pixel in front of or behind the planes (its through bit). The SH-2 keeps a copy of BG1/BG2 in Genesis video RAM in step with the SNES VRAM (only what changed: ~60 words a frame on average) and the 68000 applies it in the V-blank the 32X shows the frame in (`zelda3/snes/md_planes.c`, `cart/cart_md.s`). The 4 palettes with the most cells on screen get the Genesis's 4 colour lines; the few cells of other palettes are drawn by the 32X. Used whenever the frame allows (no colour math that shows, no window or mosaic on BG1/BG2): 1-99% of frames depending on the chapter (all 13: 62%). Checked on the PC: the Genesis + 32X picture is the same as the whole 8-bit picture in every one of those frames (`ZH_MDCHECK`). Backgrounds have the Genesis's 512 colours (3 bits a channel) |
| Video (other frames) | a software SNES PPU on both SH-2s (master the top lines, slave the rest, balanced every frame). Mode 1 is painted layer by layer in priority order (no depth buffer). Per frame: **8-bit** (CGRAM indices, the CGRAM as the 32X palette with the brightness) or, when the frame has **colour math** (water, fog, rain, transparent floors, dark rooms) or the colour window's clip, **15-bit direct colour** with the subscreen painted too and blended exactly as the SNES does. Mode 7 and mosaic use zelda3's own line renderer |
| Speed (ares) | game logic ~1 ms a frame; Genesis-planes frames ~14 ms to draw (**~29 fps**, a room of chapter 3); 8-bit frames ~43 ms (**~15 fps**), colour-math frames ~120 ms (**~10 fps**, the game slows a little there); frame skip up to 6 |
| Sound effects | every effect the game can send (ports 1-3: effects, the rain / wind ambiences) rendered at build time with zelda3's own SNES sound driver (`pc/zh_sound`), 8-bit at the PWM rate in the cartridge (2.1 MB); the slave SH-2 mixes 5 voices into the 32X PWM in its H interrupt, paced by the PWM's FIFO (`sh2/z32_sound.cpp`). Pitch checked in ares; the rate is one constant (`Z32_PWM_RATE`) if a console plays it off |
| Music | CD audio from a Sega CD under the 32X (Mode 1): `build/Zelda3_32X_CD.cue`, track n = song n (34 tracks, 27 min, one pass each - the drive repeats the songs that loop), rendered with the same driver. The 68000 plays them (`cart/cd.s`, from Sonic32XPlus1); 0xF1-0xF3 fade / lower / restore with the BIOS fader. No Sega CD: no music |
| Controls | Genesis pad: A = Y (item), B = B (sword), C = A (action), X = X (map), Y = L, Z = R, Start = Start, Mode = Select |
| Saves | the game's three files in the cartridge's backup SRAM (8 KB, odd bytes at 0x200001; header `RA`). The ROM's upper half shares that address, so the 68000 switches the SRAM in (0xA130F1) only while it copies it to / from an 8 KB mirror in its RAM, with both SH2s parked in SDRAM (`sh2/z32_save.cpp`, `cart/cart_md.s`); a save sends only what changed. Tested in ares: a file made, the console switched off and on, the file is there |
| Not done | the colour-math scenes on the Genesis planes (an approximation would be needed: the Genesis cannot blend); colour math with a fixed colour or subtraction runs a slower exact loop; mode 7 (the world map) at speed; stereo effects (the SNES pans them - mono here) |

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
- The Sega toolchains: `sh-elf` (the SH-2s) and `m68k-elf` (the 68000), in `/opt/toolchains/sega`
  or wherever `$GENDEV` points (e.g. `sega-toolchain-12.1`)
- `python3` with `numpy` (`pip install numpy`; `scipy` too for the best sound resampling),
  `g++` and `make`
- Your own US *A Link to the Past* ROM, `zelda3.sfc` (SHA256 `66871d66...009cfb`, above)

### Steps
1. Make the game's assets from your ROM with zelda3's own tool, and copy them to `build/`:
   ```
   cd zelda3
   cp /path/to/zelda3.sfc .
   python3 -m pip install -r requirements.txt   # zelda3's tool needs Pillow and PyYAML
   python3 assets/restool.py --extract-from-rom # writes zelda3_assets.dat
   cd ..
   mkdir -p build && cp zelda3/zelda3_assets.dat build/
   ```
2. Build:
   ```
   ./build.sh 2>&1 | tee build_log.txt
   ```
   The first build also renders the sound from the ROM's song banks (a few minutes): the sound
   effects `build/sfx.bin` and the music CD `build/Zelda3_32X_CD.cue` / `.bin`. Delete
   `build/sfx.bin` to make them again.
3. Play:
   - `Zelda3_32X.32x` is the cartridge (4 MB, 8 KB of battery SRAM for the saves). Load it in
     Kega Fusion / ares as a 32X game, or put it on a flash cart in a 32X.
   - Music: put `build/Zelda3_32X_CD.cue` in the Sega CD (burn it, or load it as the Sega CD's
     disc in the emulator) with the cartridge in the 32X. Without a Sega CD the game has no music.

### Other builds
```
./build.sh replay                            # the chapter-1 recording plays itself, then the pad
./build.sh test                              # the recording, checked against the PC every frame
```
`replay` and `test` need the test data:
```
make -C pc -f Makefile.cpp                   # the PC reference, pc/zhx
python3 tools/make_testdata.py build/zelda3_assets.dat build
```

### Switches and test aids
`sh2/z32_main.cpp` options (the `DEFS` line of `build.sh`): `-DZ32_NO_CHECK` (no RAM / picture
checks: the real speed), `-DZ32_PLAY` (no replay: the pad from power-on), `-DZ32_NO_MD` (no
Genesis planes: every frame drawn whole by the 32X), `-DZ32_NO_SKIP` (draw every frame),
`-DZ32_INDEX8` / `-DZ32_DIRECT` (one picture mode for every frame), `-DZ32_STATS_FROM=n` (the
timings from game frame n on), `-DZ32_PPU_TIMING` / `-DZ32_HYB_TIMING` / `-DZ32_MD_PROF` (FRT
ticks per drawing stage), `-DZ32_SAVE_TEST` (the SRAM self test), `-DZ32_STOP_ON_BAD` (stop at
the first mismatch, show that picture). `pc/zhx` test aids: `ZH_REPLAY=n` (chapter),
`ZH_HASH`, `ZH_PIXHASH`, `ZH_DRAWALL`, `ZH_AUTO` (the 32X's picture mode per frame) /
`ZH_RGB15` (direct colour always), `ZH_REF` (zelda3's own line renderer: the reference for the
painted one), `ZH_SPLIT=line` (the two-CPU drawing, one part after the other), `ZH_MDCHECK`
(the Genesis planes + 32X picture against the whole one, every frame; `ZH_MDBAD=dir` writes
the first that differ), `ZH_VRAMCHECK` (every VRAM write was marked for the Genesis's copy),
`ZH_RAMAT=frame` + `ZH_RAMFILE`, `ZH_PPUDBG=frame`.

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
