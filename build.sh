#!/bin/bash
# Zelda 3 32X - builds the cartridge.            ./build.sh [play|replay|test] 2>&1 | tee build_log.txt
#   play    (default) the pad from power-on                         -> Zelda3_32X.32x
#   replay  the chapter-1 recording, then the pad                   -> Zelda3_32X-replay.32x
#   test    the replay with the RAM / picture checks against the PC -> Zelda3_32X-test.32x
# Needs build/zelda3_assets.dat (from your ROM: cd zelda3 && python3 assets/restool.py, then
# copy zelda3_assets.dat to build/) and, for replay / test, build/replay.bin, expect.bin,
# expect_px.bin (python3 tools/make_testdata.py build/zelda3_assets.dat build).
# The sound (build/sfx.bin, the music CD build/Zelda3_32X_CD.cue) is made on the first build:
# pc/zh_sound renders it with zelda3's SNES sound driver (needs g++), tools/make_sound.py packs
# it (needs python3 with numpy; scipy for the best resampling). Delete build/sfx.bin to make it
# again. The music CD (Mode 1: the Sega CD under the 32X, this disc in its drive) is an audio CD,
# track n = song n: burn build/Zelda3_32X_CD.cue, or load it in an emulator as the Sega CD's disc.
# The toolchains: $GENDEV or /opt/toolchains/sega (sh-elf and m68k-elf), python3.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOTDIR="${GENDEV:-/opt/toolchains/sega}"
M68K="$ROOTDIR/m68k-elf/bin/m68k-elf-"
VARIANT="${1:-play}"
case "$VARIANT" in
  play)   DEFS="-DZ32_NO_CHECK -DZ32_PLAY"; ROM=Zelda3_32X.32x ;;
  replay) DEFS="-DZ32_NO_CHECK";            ROM=Zelda3_32X-replay.32x ;;
  test)   DEFS="";                          ROM=Zelda3_32X-test.32x ;;
  *) echo "usage: ./build.sh [play|replay|test]"; exit 1 ;;
esac
DATA="$HERE/build"
[ -f "$DATA/zelda3_assets.dat" ] || { echo "*** $DATA/zelda3_assets.dat is missing (see the top of build.sh)"; exit 1; }
for f in replay.bin expect.bin expect_px.bin; do      # the SH2 program links them (empty for play)
  [ -f "$DATA/$f" ] || { [ "$VARIANT" = play ] && : > "$DATA/$f" || { echo "*** $DATA/$f is missing: python3 tools/make_testdata.py build/zelda3_assets.dat build"; exit 1; }; }
done

if [ ! -f "$DATA/sfx.bin" ] || [ ! -f "$DATA/Zelda3_32X_CD.cue" ]; then   # the sound, from your ROM's song banks
  echo "== sound: effects (build/sfx.bin) and the music CD (build/Zelda3_32X_CD.cue / .bin)"
  make -C "$HERE/pc" -f Makefile.cpp zh_sound
  mkdir -p "$DATA/snd"
  "$HERE/pc/zh_sound" "$DATA/zelda3_assets.dat" "$DATA/snd"
  python3 "$HERE/tools/make_sound.py" "$DATA/snd" "$DATA"
fi

echo "== 68000 side (cart/cart_md.s)"
cd "$HERE/cart"
"${M68K}as" -m68000 --register-prefix-optional --defsym HW_BUILD=1 -o cart_md.o cart_md.s
"${M68K}ld" -Ttext 0x880800 -o cart_md.elf cart_md.o
"${M68K}objcopy" -O binary cart_md.elf cart_md.bin

echo "== SH2 program ($VARIANT: $DEFS)"
cd "$HERE/sh2"
make clean > /dev/null
make -j"$(nproc)" ROOTDIR="$ROOTDIR" Z32_DATA="$DATA" DEFS="$DEFS"

echo "== cartridge"
cd "$HERE"
python3 tools/make_cart.py sh2/z32.bin cart/cart_md.bin "$ROM"
echo "done: $HERE/$ROM"
