#!/usr/bin/env python3
"""endian_fix.py - Z32X: make zelda3's BYTE()/HIBYTE()/WORD()/DWORD() uses correct on the
big-endian SH-2. The SNES RAM (g_ram), the save RAM and the game data stay little-endian
(types.h LE16 / LEA16, GCC scalar_storage_order); these helpers took a variable's address,
which is only right on a little-endian CPU.

  python3 tools/endian_fix.py <zelda3 dir> [--sizes sizes.txt]

Pass 1 (no --sizes): every use whose argument is not a g_ram variable from variables.h gets a
probe macro  WPROBE(id, kind, x)  -> the build reports sizeof(x) per id (tools/endian_probe.sh).
Pass 2 (--sizes): each use is rewritten:
  g_ram word var at A : BYTE -> *(uint8*)(g_ram+A)   HIBYTE -> ...+A+1   WORD -> var   DWORD -> LE32
  g_ram byte var at A : BYTE -> var    HIBYTE -> *(uint8*)(g_ram+A+1)   WORD -> LE16(g_ram+A) ...
  g_ram arrays name[i]: by element size, the same by address
  others              : 1-byte lvalue (a byte in a buffer): WORD -> LE16(&(x)), DWORD -> LE32(&(x));
                        2-byte native: WORD -> (x); 4-byte native: WORD -> native low half;
                        BYTE / HIBYTE -> native-order macros (types.h BYTE_N / HIBYTE_N)
"""
import re, sys, os

def load_vars(zdir):
    s = open(os.path.join(zdir, 'src/variables.h')).read()
    v = {}
    for name, a in re.findall(r'#define (\w+) \(\*\(uint8\*\)\(g_ram\+(0x[0-9A-Fa-f]+)\)\)', s): v[name] = ('u8', a)
    for name, a in re.findall(r'#define (\w+) LE16\(g_ram\+(0x[0-9A-Fa-f]+|\d+)\)', s): v[name] = ('u16', a)
    for name, a in re.findall(r'#define (\w+) \(\(uint8\*\)\(g_ram\+(0x[0-9A-Fa-f]+)\)\)', s): v[name] = ('a8', a)
    for name, a in re.findall(r'#define (\w+) LEA16\(g_ram\+(0x[0-9A-Fa-f]+)\)', s): v[name] = ('a16', a)
    return v

MAC = re.compile(r'\b(BYTE|HIBYTE|WORD|DWORD)\(')

def args_at(s, i):
    """s[i] is '(' : return (arg, end index after ')')."""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '(': depth += 1
        elif s[j] == ')':
            depth -= 1
            if depth == 0: return s[i + 1:j], j + 1
    raise ValueError('unbalanced')

def rewrite(s, vars_, sizes, fname, ids):
    out = []; pos = 0
    for m in MAC.finditer(s):
        if m.start() < pos: continue
        # skip the macro definitions themselves
        line_start = s.rfind('\n', 0, m.start()) + 1
        if s[line_start:m.start()].lstrip().startswith('#define'): continue
        kind = m.group(1)
        arg, end = args_at(s, m.end() - 1)
        a = arg.strip()
        rep = None
        mv = re.fullmatch(r'(\w+)', a)
        ma = re.fullmatch(r'(\w+)\s*\[(.*)\]', a, re.S)
        if mv and mv.group(1) in vars_:
            t, A = vars_[mv.group(1)]
            if t == 'u16':
                rep = {'BYTE': '(*(uint8*)(g_ram+%s))' % A, 'HIBYTE': '(*(uint8*)(g_ram+%s + 1))' % A,
                       'WORD': a, 'DWORD': 'LE32(g_ram+%s)' % A}[kind]
            elif t == 'u8':
                rep = {'BYTE': a, 'HIBYTE': '(*(uint8*)(g_ram+%s + 1))' % A,
                       'WORD': 'LE16(g_ram+%s)' % A, 'DWORD': 'LE32(g_ram+%s)' % A}[kind]
        elif ma and ma.group(1) in vars_ and vars_[ma.group(1)][0] in ('a8', 'a16'):
            t, A = vars_[ma.group(1)]; idx = ma.group(2)
            if t == 'a8':
                rep = {'BYTE': a, 'HIBYTE': '(*(uint8*)(g_ram+%s + (%s) + 1))' % (A, idx),
                       'WORD': 'LE16(g_ram+%s + (%s))' % (A, idx), 'DWORD': 'LE32(g_ram+%s + (%s))' % (A, idx)}[kind]
            else:
                rep = {'BYTE': '(*(uint8*)(g_ram+%s + 2*(%s)))' % (A, idx), 'HIBYTE': '(*(uint8*)(g_ram+%s + 2*(%s) + 1))' % (A, idx),
                       'WORD': a, 'DWORD': 'LE32(g_ram+%s + 2*(%s))' % (A, idx)}[kind]
        if rep is None:
            sid = '%s:%d' % (fname, len(ids)); ids.append(sid)
            if kind in ('BYTE', 'HIBYTE'):
                rep = '%s_N(%s)' % (kind, arg)          # native order (types.h)
            elif sizes is None:
                rep = 'WPROBE(%d, %s)' % (len(ids) - 1, arg)
            else:
                sz = sizes.get(sid)
                if sz is None: raise SystemExit('no size for ' + sid)
                if sz == 1:
                    rep = ('LE16(&(%s))' if kind == 'WORD' else 'LE32(&(%s))') % arg
                elif sz == 2 and kind == 'WORD':
                    rep = '(%s)' % arg
                elif sz == 4 and kind == 'DWORD':
                    rep = '(%s)' % arg
                elif sz == 4 and kind == 'WORD':
                    rep = 'WORD_LO_N(%s)' % arg
                else:
                    raise SystemExit('%s: %s of a %d-byte value: %s' % (sid, kind, sz, arg))
        out.append(s[pos:m.start()]); out.append(rep); pos = end
    out.append(s[pos:])
    return ''.join(out)

def main():
    zdir = sys.argv[1]
    sizes = None
    if '--sizes' in sys.argv:
        sizes = {}
        for line in open(sys.argv[sys.argv.index('--sizes') + 1]):
            k, v = line.split(); sizes[k] = int(v)
    vars_ = load_vars(zdir)
    for f in sorted(os.listdir(os.path.join(zdir, 'src'))):
        if not f.endswith(('.c', '.h')) or f in ('types.h', 'variables.h', 'main.c', 'opengl.c', 'glsl_shader.c'): continue
        p = os.path.join(zdir, 'src', f)
        s = open(p).read()
        if not MAC.search(s): continue
        ids = []
        n = rewrite(s, vars_, sizes, f, ids)
        if sizes is None:
            open(p + '.probe', 'w').write(n)
            open(p + '.probeids', 'w').write('\n'.join(ids))
        else:
            open(p, 'w').write(n)
        print(f, len(ids), 'non-RAM uses')

if __name__ == '__main__':
    main()
