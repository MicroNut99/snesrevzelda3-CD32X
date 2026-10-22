# Zelda 3 32X - A Link to the Past on the Sega 32X

A port of [snesrev/zelda3](https://github.com/snesrev/zelda3) (MIT), the C reimplementation of
*The Legend of Zelda: A Link to the Past*, to the **Sega 32X as a cartridge game**, with music from
a Sega CD (CD audio, Rev32XCD / Sonic32XPlus1's CD code) when one is attached.

**No Nintendo data is in this repository.** Like zelda3 itself, the game's graphics, levels and
music are extracted at build time from your own US ROM (`zelda3.sfc`, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

## Status: plays in ares - game at full speed, picture ~16 fps

The SH-2 runs zelda3's game code at the SNES's 60 Hz and draws as often as it can (frame skip).
With the test build it replays the project's chapter-1 recording and, after every game frame,
compares the SNES RAM - and every drawn picture - with the PC reference (`pc/zhx`): all 7157 game
frames and 1193 pictures of the last run match. The `Z32_PLAY` build plays from power-on with the
pad.

| Part | State |
|---|---|
| Assets from the ROM | 684 KB (`zelda3_assets.dat`); with ~750 KB of SH-2 code the game fits a 4 MB cartridge |
| Memory | SNES WRAM 128 KB + PPU ~8 KB + VRAM 64 KB + SRAM 8 KB; heap peak ~232 KB of the 32X's 256 KB |
| Byte order | the SNES RAM, VRAM and data stay little-endian; all game code is C++ with `le16` / `lei16` / `le32` (`types.h`), so a native pointer into SNES memory is a compile error. Checked on the PC with the 13 chapter recordings (~1.07 million frames, RAM and picture compared every frame) |
| Video | a software SNES PPU: mode 1 is painted layer by layer in priority order into an 8-bit line (no depth buffer; the same picture as zelda3's), copied into the 32X frame buffer; the SNES CGRAM is the 32X palette (same 15-bit format), with the SNES brightness applied as it is copied. Both SH-2s draw (master the top lines, slave the rest, balanced every frame). Mode 7 and mosaic use zelda3's own line renderer |
| Speed (ares) | game logic ~1.7 ms a frame; drawing ~42 ms on each SH-2: **game 100% speed, picture ~16 fps** (frame skip up to 6) |
| Controls | Genesis pad: A = Y (item), B = B (sword), C = A (action), X = X (map), Y = L, Z = R, Start = Start, Mode = Select |
| Not done | colour math (water, dark rooms, the lantern), mode 7 (the world map) at speed; SRAM saves; music (CD audio) and effects (PWM) |

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
- The CRAM takes writes only while PEN (FBCTL bit 13) is set.

## Building
```
./build.sh 2>&1 | tee build_log.txt          # play (pad from power-on) -> Zelda3_32X.32x
./build.sh replay                            # the chapter-1 replay, then the pad
./build.sh test                              # the replay, checked against the PC every frame
```
Needs `build/zelda3_assets.dat` from your ROM (zelda3's own tool: `cd zelda3 && python3
assets/restool.py`, then copy it to `build/`), the sh-elf and m68k-elf toolchains (`$GENDEV` or
`/opt/toolchains/sega`) and python3. For `replay` / `test`, the test data:
```
make -C pc -f Makefile.cpp                   # the PC reference, pc/zhx
python3 tools/make_testdata.py build/zelda3_assets.dat build
```
`sh2/z32_main.cpp` options: `-DZ32_NO_CHECK` (no RAM / picture checks: the real speed),
`-DZ32_PLAY` (no replay: the pad from power-on), `-DZ32_NO_SKIP` (draw every frame),
`-DZ32_STOP_ON_BAD` (stop at the first mismatch, show that picture). `pc/zhx` test aids:
`ZH_REPLAY=n` (chapter), `ZH_HASH`, `ZH_PIXHASH`, `ZH_DRAWALL`, `ZH_SPLIT=line` (the two-CPU
drawing, one part after the other), `ZH_RAMAT=frame` + `ZH_RAMFILE`, `ZH_PPUDBG=frame`.

## Layout
- `zelda3/` - snesrev/zelda3 (MIT), with the port's changes (marked `Z32X`)
- `pc/` - the headless PC runner, the reference for the 32X build
- `sh2/` - the SH-2 program: start-up (`crt0.s`, from Rev32XCD), linker script, the test harness
  and two-CPU drawing (`z32_main.cpp`), sound stubs
- `cart/` - the 68000 program (Rev32XCD's cartridge `cart_md.s`, starfield removed), CD code
- `tools/` - `make_cart.py` (32X cartridge image), `make_testdata.py`, `endian_fix.py` /
  `le_ptr_fix.py` (the byte-order rewrite), `mars_boot.bin`
