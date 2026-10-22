#!/usr/bin/env python3
"""Zelda 3 32X - the game's sound for the cartridge and the CD, from pc/zh_sound's renders.

  python3 tools/make_sound.py <zh_sound folder> <build folder>

Writes into <build folder>:
  sfx.bin             the sound effects, for the cartridge (sh2/z32_data.s): signed 8-bit mono
                      at the PWM rate (Z32_PWM_RATE, the same constant as sh2/z32_sound.h), the
                      repeating ambiences cut to a 2 s loop with a crossfade at the seam,
                      identical sounds stored once, plus the songs' repeat flags
  Zelda3_32X_CD.bin   the music CD for the Sega CD (Mode 1: the cartridge plays it): audio
  Zelda3_32X_CD.cue   track n = song n (01..34), 44100 Hz stereo, one pass of each song - the CD
                      player repeats the track for the songs that loop
sfx.bin layout (big-endian): 'ZSFX', u32 rate, 64 song flags (bit 0 exists, bit 1 repeats),
then for port 1..3 x id 0..63: u32 offset, u32 length, u32 loop start (0xFFFFFFFF: none) -
offsets from the start of sfx.bin - then the samples.
"""
import hashlib, os, struct, sys
import numpy as np
try:
    from scipy.signal import resample_poly            # the good resampler
except ImportError:                                   # without scipy: linear (pip install scipy)
    def resample_poly(x, up, down, axis=0):
        x = np.asarray(x, dtype=np.float64)
        n = len(x) * up // down
        t = np.arange(n) * down / up
        if x.ndim == 1:
            return np.interp(t, np.arange(len(x)), x)
        return np.stack([np.interp(t, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], axis=1)
    print('(scipy not found: linear resampling)')

PWM_RATE = 15720            # Z32_PWM_RATE (sh2/z32_sound.h): the PWM's sample rate
SRC_RATE = 32000            # the SNES DSP
LOOP_SECONDS = 2.0
FADE_SECONDS = 0.25

def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)

    # ---- sound effects ----
    rows = [l.split() for l in open(os.path.join(src, 'sfx.txt'))]
    from math import gcd
    g = gcd(PWM_RATE, SRC_RATE)
    up, down = PWM_RATE // g, SRC_RATE // g
    sounds = {}
    for p, i, frames, loops in rows:
        a = np.fromfile(os.path.join(src, 'sfx_%s_%s.raw' % (p, i)), dtype='<i2').astype(np.float64)
        b = resample_poly(a, up, down)
        loop_start = None
        if loops == '1':
            # an ambience still playing at the end of the render: 1 s in, a 2 s loop whose last
            # quarter second fades into the quarter second before the loop start (a smooth seam)
            s0, n = int(PWM_RATE * 1.0), int(PWM_RATE * LOOP_SECONDS)
            f = int(PWM_RATE * FADE_SECONDS)
            b = b[:s0 + n].copy()
            w = np.linspace(0.0, 1.0, f)
            b[s0 + n - f:s0 + n] = b[s0 + n - f:s0 + n] * (1 - w) + b[s0 - f:s0] * w
            loop_start = s0
        else:
            peak = np.abs(b).max()
            nz = np.nonzero(np.abs(b) > max(peak / 128, 64))[0]
            b = b[:nz[-1] + 1] if len(nz) else b[:0]
        sounds[(int(p), int(i, 16))] = (b, loop_start)
    peak = max(np.abs(b).max() for b, _ in sounds.values() if len(b))
    gain = 110.0 / peak                                       # the loudest effect at +-110
    data = bytearray()
    table = {}
    seen = {}
    for key, (b, loop_start) in sorted(sounds.items()):
        s8 = np.clip(np.round(b * gain), -127, 127).astype(np.int8).tobytes()
        h = hashlib.md5(s8).hexdigest() + str(loop_start)
        if h not in seen:
            seen[h] = len(data)
            data += s8
            while len(data) & 3: data += b'\0'
        table[key] = (seen[h], len(s8), loop_start)
    flags = bytearray(64)
    songs = [l.split() for l in open(os.path.join(src, 'songs.txt'))]
    for nn, bank, frames, loops in songs:
        flags[int(nn, 16)] = 1 | (2 if loops == '1' else 0)
    head = bytearray(b'ZSFX') + struct.pack('>I', PWM_RATE) + flags
    tsize = 3 * 64 * 12
    base = len(head) + tsize
    for p in (1, 2, 3):
        for i in range(64):
            if (p, i) in table:
                off, n, ls = table[(p, i)]
                head += struct.pack('>III', base + off, n, 0xFFFFFFFF if ls is None else ls)
            else:
                head += struct.pack('>III', 0, 0, 0xFFFFFFFF)
    open(os.path.join(out, 'sfx.bin'), 'wb').write(bytes(head) + bytes(data))
    print('sfx.bin: %d effects (%d stored), %d KB at %d Hz' % (len(table), len(seen), (len(head) + len(data)) // 1024, PWM_RATE))

    # ---- the music CD: track n = song n ----
    cue = ['FILE "Zelda3_32X_CD.bin" BINARY']
    pos = 0                                                   # in 2352-byte sectors
    with open(os.path.join(out, 'Zelda3_32X_CD.bin'), 'wb') as cd:
        present = {int(s[0], 16) for s in songs}
        for n in range(1, max(present) + 1):
            if n in present:
                a = np.fromfile(os.path.join(src, 'song_%02x.raw' % n), dtype='<i2').reshape(-1, 2).astype(np.float64)
                b = resample_poly(a, 441, 320, axis=0)        # 32000 -> 44100 Hz
                pcm = np.clip(np.round(b), -32768, 32767).astype('<i2').tobytes()
            else:
                pcm = bytes(2352 * 75)                        # a second of silence: keeps n = song n
            pcm += bytes((-len(pcm)) % 2352)
            cue.append('  TRACK %02d AUDIO' % n)
            if n == 1:
                cue.append('    INDEX 01 00:00:00')
            else:
                cue.append('    INDEX 00 %s' % msf(pos))
                cd.write(bytes(2352 * 150))                   # 2 s pregap
                pos += 150
                cue.append('    INDEX 01 %s' % msf(pos))
            cd.write(pcm)
            pos += len(pcm) // 2352
    open(os.path.join(out, 'Zelda3_32X_CD.cue'), 'w').write('\n'.join(cue) + '\n')
    print('Zelda3_32X_CD.bin/.cue: %d tracks, %d:%02d' % (max(present), pos // 75 // 60, pos // 75 % 60))

def msf(sectors):
    return '%02d:%02d:%02d' % (sectors // 4500, sectors // 75 % 60, sectors % 75)

if __name__ == '__main__':
    main()
