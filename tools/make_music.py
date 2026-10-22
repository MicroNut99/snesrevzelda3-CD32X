#!/usr/bin/env python3
"""make_music.py - a VGM (YM2612 + SN76489) -> U-R-R's frame stream for the 68000 player.

    python3 tools/make_music.py cart/music/song.vgm cart/music_stream.bin
    python3 tools/make_music.py --empty cart/music_stream.bin     (no song)

The 68000 plays one frame of the stream per V-blank (cart/cart_md.s, vgm_tick), so VGM's
44.1 kHz sample waits are turned into frame waits here (735 samples = one 60 Hz frame; the
remainder is carried, so the tempo stays exact over the song).  PAL consoles (50 Hz) play an
extra frame every 5th V-blank.

Stream (big-endian):
    +0  u32  loop offset (from the start of the commands; the VGM's loop point, else 0)
    +4  commands:
        0x52 reg val    YM2612 port 0 (channels 1-3, global registers)
        0x53 reg val    YM2612 port 1 (channels 4-6)
        0x50 val        SN76489 PSG
        0x61 hi lo      wait this many frames (1..65535)
        0x66            end: go to the loop offset
DAC sample writes (0x80-0x8F) and data blocks are dropped (their waits are kept)."""
import struct, sys

if sys.argv[1] == "--empty":                  # no song: a silent stream (wait, end), FM unused
    open(sys.argv[2], "wb").write(struct.pack(">I", 0) + bytes((0x61, 0xFF, 0xFF, 0x66)))
    print("  music: no VGM in cart/music - no FM song (MUSIC option: CD / OFF)")
    sys.exit(0)
src, dst = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()
assert d[:4] == b"Vgm ", "not a VGM file"
ver = struct.unpack_from("<I", d, 8)[0]
data = 0x34 + struct.unpack_from("<I", d, 0x34)[0] if ver >= 0x150 and struct.unpack_from("<I", d, 0x34)[0] else 0x40
loop_abs = 0x1C + struct.unpack_from("<I", d, 0x1C)[0] if struct.unpack_from("<I", d, 0x1C)[0] else 0
SPF = 735                                     # samples per 60 Hz frame
out = bytearray(); loop_out = 0
acc = 0                                       # samples not yet turned into frames
def flush():
    global acc
    n = acc // SPF
    acc -= n * SPF
    while n > 0:
        k = min(n, 65535)
        out.extend((0x61, k >> 8, k & 255)); n -= k
p = data; dropped = 0; writes = 0
while p < len(d):
    if p == loop_abs:
        flush(); loop_out = len(out)
    op = d[p]
    if op in (0x52, 0x53):
        flush(); out.extend((op, d[p + 1], d[p + 2])); p += 3; writes += 1
    elif op == 0x50:
        flush(); out.extend((0x50, d[p + 1])); p += 2; writes += 1
    elif op == 0x61: acc += struct.unpack_from("<H", d, p + 1)[0]; p += 3
    elif op == 0x62: acc += 735; p += 1
    elif op == 0x63: acc += 882; p += 1
    elif 0x70 <= op <= 0x7F: acc += (op & 15) + 1; p += 1
    elif 0x80 <= op <= 0x8F: acc += op & 15; p += 1; dropped += 1
    elif op == 0x67: p += 7 + struct.unpack_from("<I", d, p + 3)[0]
    elif op == 0xE0: p += 5
    elif op == 0x4F: p += 2                   # Game Gear stereo: ignore
    elif op == 0x66: break
    else: sys.exit("*** unsupported VGM command 0x%02X at 0x%X" % (op, p))
flush()
if acc: out.extend((0x61, 0, 1))              # the last part of a frame
out.append(0x66)
open(dst, "wb").write(struct.pack(">I", loop_out) + out)
print("  music: %s -> %d bytes, %d register writes, loop at %d%s" % (
    src.split("/")[-1], len(out) + 4, writes, loop_out, ", %d DAC writes dropped" % dropped if dropped else ""))
