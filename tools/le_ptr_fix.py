#!/usr/bin/env python3
"""le_ptr_fix.py - Z32X: from the C++ build's "cannot convert 'le16*' to 'uint16*' in
initialization" errors (a pointer into SNES memory declared as a native uint16 pointer),
change that declaration to le16 (lei16 / le32 likewise). Run from pc/: reads the error list.
  python3 ../tools/le_ptr_fix.py errors.txt"""
import re, sys, collections
fix = collections.defaultdict(set)
PARAMS = "--params" in sys.argv
for line in ([] if PARAMS else open(sys.argv[1])):
    m = re.match(r"([^:]+):(\d+):\d+: error: cannot convert '(le16|lei16|le32)\*'.*to '(const )?(uint16|int16|uint32)\*'.* in initialization", line)
    if m and not m.group(1).endswith('.h'):
        fix[m.group(1)].add((int(m.group(2)), m.group(3), m.group(5)))
n = 0
for f, items in fix.items():
    L = open(f).read().split('\n')
    for ln, le, nat in items:
        s = L[ln - 1]
        s2 = re.sub(r'\b%s(\s*\*)' % nat, le + r'\1', s, count=1)
        if s2 != s: L[ln - 1] = s2; n += 1
        else: print('not changed', f, ln, s.strip())
    open(f, 'w').write('\n'.join(L))
if not PARAMS: print(n, 'declarations changed')

def fix_params(errfile, srcdir):
    """'initializing argument N of F(...)' notes after a le16* -> uint16* error: make that
    parameter of F a le16 pointer in every declaration and definition."""
    import glob, os
    want = set()
    lines = open(errfile).read().split('\n')
    for i, line in enumerate(lines):
        if re.search(r"cannot convert '(le16|lei16|le32)\*'", line):
            le = re.search(r"cannot convert '(le16|lei16|le32)\*'", line).group(1)
            for j in range(i + 1, min(i + 12, len(lines))):
                m = re.search(r"initializing argument (\d+) of '[^']*?\b(\w+)\(", lines[j])
                if m: want.add((m.group(2), int(m.group(1)), le)); break
    n = 0
    for f in glob.glob(os.path.join(srcdir, '*.[ch]')):
        s = open(f).read(); orig = s
        for name, argn, le in want:
            def repl(m):
                params = m.group(2).split(',')
                if argn - 1 < len(params):
                    p = params[argn - 1]
                    p2 = re.sub(r'\b(uint16|int16|uint32)(\s*\*)', le + r'\2', p, count=1)
                    params[argn - 1] = p2
                return m.group(1) + ','.join(params) + m.group(3)
            s = re.sub(r'(^[\w \t\*]*\b%s\s*\()([^()]*)(\)\s*[{;])' % name, repl, s, flags=re.M)
        if s != orig: open(f, 'w').write(s); n += 1
    print(len(want), 'parameters,', n, 'files changed')

if len(sys.argv) > 2 and sys.argv[2] == '--params':
    fix_params(sys.argv[1], sys.argv[3])
