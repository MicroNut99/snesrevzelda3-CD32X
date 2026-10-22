#!/usr/bin/env python3
"""make_cart.py - build the Zelda 3 32X cartridge ROM (U-R-R's / Kobo's cartridge builder).

  python3 tools/make_cart.py sh2/z32.bin cart/cart_md.bin Zelda3_32X.32x

ROM layout:
  0x000000  standard 32X boot block (tools/mars_boot.bin), patched: titles (ROM_TITLE), ROM end,
            backup SRAM (8 KB at 0x200001, odd bytes), the 32X header's SH2 values (from the
            program's 0x38-byte boot table), checksum
  0x000800  cart_md.bin - the 68000 program (linked at 0x880800)
  0x008000  the SH2 program (boot table removed); the 32X boot ROM copies its first part
            (code + data, the boot table's size) to SDRAM
  after it  the game's data (section .romdata: assets, sound effects), read in place by the SH2
The ROM is padded to a power of two, at least 256 KB."""
import sys, os, struct
sh2_path, md_path, out = sys.argv[1:4]
TITLE = os.environ.get("ROM_TITLE", "ZELDA 3 32X")
HERE = os.path.dirname(os.path.abspath(__file__))
boot = bytearray(open(os.path.join(HERE, "mars_boot.bin"), "rb").read())
assert len(boot) == 0x800 and boot[0x100:0x108] == b"SEGA 32X", "bad tools/mars_boot.bin"
sh2 = open(sh2_path, "rb").read()
md = open(md_path, "rb").read()
size, ment, sent, mvbr, svbr = struct.unpack(">5I", sh2[0x1C:0x30])
assert sh2[0x18:0x1C] == b"\x06\x00\x00\x00" and size <= len(sh2) - 0x38, "SH2 program: bad boot table"
SH2_OFF = 0x8000
SIZE = 0x40000                                                 # v17: grows with the skins' ROM data
while SH2_OFF + len(sh2) - 0x38 > SIZE: SIZE *= 2
if SIZE > 0x400000: sys.exit("*** more than 4 MB")
if 0x800 + len(md) > SH2_OFF: sys.exit("*** 68000 program too big (%d bytes)" % len(md))

rom = bytearray(b"\xFF" * SIZE)
rom[0:0x800] = boot
def put_str(off, s, n): rom[off:off + n] = s.encode("ascii")[:n].ljust(n, b" ")
put_str(0x120, TITLE, 48); put_str(0x150, TITLE, 48)
rom[0x1A0:0x1A8] = struct.pack(">II", 0, SIZE - 1)
# backup SRAM: 8 KB on the odd bytes of 0x200001-0x203FFF (Zelda's save file). A 4 MB ROM's
# upper half is there too: the 68000 switches the SRAM in with 0xA130F1 only while it copies it
# (cart/cart_md.s sram_blob), the way 3-4 MB Genesis games with saves do
rom[0x1B0:0x1BC] = b"RA\xF8\x20" + struct.pack(">II", 0x200001, 0x203FFF)
put_str(0x3C0, TITLE, 16)                                      # 32X header
rom[0x3D0:0x3F0] = struct.pack(">8I", 0, SH2_OFF, 0, size, ment, sent, mvbr, svbr)
rom[0x800:0x800 + len(md)] = md
rom[SH2_OFF:SH2_OFF + len(sh2) - 0x38] = sh2[0x38:]   # program + ROM data
ck = 0
for i in range(0x200, len(rom), 2): ck = (ck + (rom[i] << 8 | rom[i + 1])) & 0xFFFF
rom[0x18E:0x190] = struct.pack(">H", ck)
open(out, "wb").write(rom)
print("  68000 program at 0x000800, %d bytes" % len(md))
print("  SH2 program   at 0x%06X, %d bytes to SDRAM, master 0x%08X slave 0x%08X" % (SH2_OFF, size, ment, sent))
print("  ROM data      %d bytes (skins), read in place" % (len(sh2) - 0x38 - size))
print("written %s (%d KB, checksum 0x%04X)" % (out, SIZE // 1024, ck))
