# Zelda 3 32X - A Link to the Past on the Sega 32X

A port of [snesrev/zelda3](https://github.com/snesrev/zelda3) (MIT), the C reimplementation of
*The Legend of Zelda: A Link to the Past*, to the **Sega 32X as a cartridge game**, with music from
a Sega CD (CD audio, Rev32XCD / Sonic32XPlus1's CD code) when one is attached.

**No Nintendo data is in this repository.** Like zelda3 itself, the game's graphics, levels and
music are extracted at build time from your own US ROM (`zelda3.sfc`, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

## Status: runs in ares (test harness) - not yet playable

The SH-2 runs zelda3's game code and replays the project's chapter-1 recording from power-on.
After every frame it compares the SNES RAM **and the picture** with the PC reference
(`pc/zhx`): both match for all 8000 frames checked.

| Part | State |
|---|---|
| Assets from the ROM | 684 KB (`zelda3_assets.dat`); with ~750 KB of SH-2 code the game fits a 4 MB cartridge |
| Memory | SNES WRAM 128 KB + PPU 4.4 KB + VRAM 64 KB + SRAM 8 KB; heap peak ~224 KB of the 32X's 256 KB |
| Byte order | the SNES RAM, VRAM and data stay little-endian; all game code is C++ with `le16` / `lei16` / `le32` (`types.h`), so a native pointer into SNES memory is a compile error. Checked on the PC with the 13 chapter recordings (~1.07 million frames, RAM and picture compared every frame) |
| Video | a software SNES PPU writes 8-bit CGRAM indices straight into the 32X frame buffer; the SNES CGRAM is copied 1:1 to the 32X palette (same 15-bit format). Both SH-2s draw: the master the top lines, the slave the rest, the split balanced every frame |
| Speed (ares) | game logic ~1.4 ms a frame; drawing ~54 ms on each SH-2 -> **~16 fps** average over the first 3300 frames. Next: hot PPU code in SDRAM, cheaper line compositing |
| Not done | colour math (water, dark rooms, the lantern), brightness fades, mode 7 (the world map) at speed; the controller; SRAM saves; music (CD audio) and effects (PWM) |

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
# PC reference (headless): pc/zhx
make -C pc -f Makefile.cpp
# assets from your ROM: zelda3's own tool (zelda3/assets/restool.py) -> zelda3_assets.dat
# SH-2 test data: the chapter's recording + the PC's hashes
python3 tools/make_testdata.py zelda3_assets.dat build 1 8000
cp zelda3_assets.dat build/
# 68000 side (cart/): m68k-elf-as -m68000 --register-prefix-optional --defsym HW_BUILD=1, ld -Ttext 0x880800, objcopy -O binary
# SH-2 side and the cartridge
make -C sh2 Z32_DATA=../build
python3 tools/make_cart.py sh2/z32.bin cart/cart_md.bin zelda3.32x
```
`sh2/z32_main.cpp` options: `-DZ32_NO_CHECK` (no RAM / picture checks: the real speed),
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
