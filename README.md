# Zelda 3 32X - A Link to the Past on the Sega 32X

A port of [snesrev/zelda3](https://github.com/snesrev/zelda3) (MIT), the C reimplementation of
*The Legend of Zelda: A Link to the Past*, to the **Sega 32X as a cartridge game**, with music from
a Sega CD (CD audio, Rev32XCD / Sonic32XPlus1's CD code) when one is attached.

**No Nintendo data is in this repository.** Like zelda3 itself, the game's graphics, levels and
music are extracted at build time from your own US ROM (`zelda3.sfc`, SHA256
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`).

## Status: porting - nothing playable yet

| Part | State |
|---|---|
| Assets from the ROM | 684 KB (`zelda3_assets.dat`); with ~750 KB of SH-2 code the game fits a 4 MB cartridge |
| Memory | the SNES state (WRAM 128 KB, PPU + VRAM 66.6 KB, SRAM 8 KB) is ~203 KB of the 32X's 256 KB |
| Colours | the SNES CGRAM and the 32X palette are the same 15-bit format: copied 1:1 |
| **Byte order (in progress)** | zelda3 assumes a little-endian CPU; the SH-2 is big-endian. The SNES RAM and data stay little-endian; every 16-bit access goes through little-endian types (`types.h` LE16 / LEA16, `tools/endian_fix.py`). Checked on the PC against the original code with the project's 13 chapter recordings (~1.07 million frames, the SNES RAM compared after every frame) |
| Speed test on the SH-2 | next: game logic + PPU timed in ares (MicroNut99/ares_android headless) |
| Video | to be decided by the speed test: all in software on the 32X, or the SNES backgrounds on the Genesis planes (SuperTux CD32X REV's method) and the 32X for sprites and overlays |
| Music | the SNES songs rendered to CD audio tracks at build time; sound effects on the 32X's PWM |

## Layout
- `zelda3/` - snesrev/zelda3 (MIT), with the port's changes (marked `Z32X`)
- `pc/` - a headless PC runner: the reference for the 32X build (`make -C pc`; frames, RAM hashes, chapter replays)
- `tools/endian_fix.py` - rewrites zelda3's BYTE/HIBYTE/WORD/DWORD uses for a big-endian CPU
- `mars/`, `tools/make_rom.py`, `tools/mars_boot.bin` - the 32X side (in progress)
