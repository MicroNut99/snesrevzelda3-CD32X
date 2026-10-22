#!/usr/bin/env python3
"""Zelda 3 32X - the SH2 test's data, made on the PC (nothing from here is committed).

  python3 tools/make_testdata.py <zelda3_assets.dat> <out folder> [chapter 1-13] [frames]

Writes into <out folder>:
  replay.bin     the chapter's recording from zelda3/saves/ref: its 32-byte header + input log
                 (the chapters start from power-on, so the snapshot part is not needed)
  expect.bin     the PC's SNES RAM hash after every frame (FNV-1a, big-endian words)
  expect_px.bin  the PC's picture hash per frame, as the 32X draws it: the 15-bit direct colour
                 picture for frames with colour math (PpuNeedsDirectColour), the 8-bit CGRAM
                 indices + the CGRAM for the others
Both hash files come from pc/zhx (make -C pc -f Makefile.cpp) drawing every frame as the game
does (ZH_DRAWALL: the draw changes the game RAM, zelda_rtl.c). Copy zelda3_assets.dat into the
same folder and build the SH2 program with make -C sh2 Z32_DATA=<out folder>.
"""
import glob, os, struct, subprocess, sys, tempfile

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    assets, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    chapter = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    frames = int(sys.argv[4]) if len(sys.argv) > 4 else 8000
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    zdir = os.path.join(root, 'zelda3')
    sav = glob.glob(os.path.join(zdir, 'saves', 'ref', 'Chapter %d - *.sav' % chapter))
    if not sav:
        sys.exit('no recording for chapter %d in zelda3/saves/ref' % chapter)
    data = open(sav[0], 'rb').read()
    hdr = struct.unpack('<8I', data[:32])
    if hdr[0] != 1 or hdr[5] & 1:
        sys.exit('%s: not a power-on recording' % sav[0])
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'replay.bin'), 'wb').write(data[:32 + hdr[2]])

    zhx = os.path.join(root, 'pc', 'zhx')
    if not os.path.exists(zhx):
        sys.exit('build the PC reference first: make -C pc -f Makefile.cpp')
    with tempfile.TemporaryDirectory() as tmp:
        ram, px = os.path.join(tmp, 'ram.txt'), os.path.join(tmp, 'px.txt')
        env = dict(os.environ, ZH_REPLAY=str(chapter - 1), ZH_DRAWALL='1', ZH_HASH=ram, ZH_PIXHASH=px)
        env['ZH_AUTO'] = '1'                     # as the 32X: direct colour for the frames that need it
        subprocess.run([zhx, assets, str(frames)], cwd=zdir, env=env, check=True,
                       stdout=subprocess.DEVNULL)
        for src, name in ((ram, 'expect.bin'), (px, 'expect_px.bin')):
            hashes = [int(line.split()[1], 16) for line in open(src)]
            open(os.path.join(out, name), 'wb').write(b''.join(struct.pack('>I', h) for h in hashes))
    print('%s: chapter %d, %d frames -> replay.bin, expect.bin, expect_px.bin' % (out, chapter, frames))

if __name__ == '__main__':
    main()
