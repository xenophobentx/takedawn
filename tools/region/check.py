"""Keep the NTSC-U and PAL builds in step, without either executable.

    python3 -m tools.region.check          (exit status 1 on any problem)

tools/region/port_config.py and xbemap.py need both default.xbe; this needs
neither, so it can run on every change (tools/region/tests). It checks:

  config        config/pal/{patches,seed_functions,spin_waits}.json is
                config/*.json entry for entry: as many entries, in the same
                order, each the same apart from its address (kind, setting,
                when, value, expr, name, latch, the "if" register and its
                list's length), and each PAL note starting "(NTSC-U <va>)"
                with the NTSC entry's own address. A new NTSC site without
                its PAL twin is an error: run port_config.py.
  wraps         every function recomp_manual.c wraps has its PAL twin in
                recomp_manual_pal.c (the PAL definition's comment names the
                NTSC function), with the same handler.
  the C         no guest address (0x11000 .. 0x778600) and no sub_XXXXXXXX
                in the code of src/game outside BO3_VA(ntsc, pal) and
                BO3_SUB(ntsc, pal) (comments and strings aside); a number in
                that range that is not a guest address says so on its line
                with "region: not an address". Every BO3_VA / BO3_SUB pair
                agrees with every other one with the same NTSC value, and
                is not the NTSC value twice unless the line says
                "region: same".
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LO, HI = 0x11000, 0x778600
SAME_KEYS = ('kind', 'setting', 'when', 'value', 'expr', 'name', 'latch')
NOT_ADDR = 'region: not an address'
SAME_OK = 'region: same'


def _load(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return json.load(f)


def _note_va(note):
    m = re.match(r'\(NTSC-U (0x[0-9A-Fa-f]{8})\) ', note or '')
    return m.group(1).upper() if m else None


def check_config(problems):
    def pair(what, ntsc, pal, va_key):
        if len(ntsc) != len(pal):
            problems.append('%s: %d NTSC-U entries, %d PAL (run tools/region/port_config.py)'
                            % (what, len(ntsc), len(pal)))
            return
        for i, (n, p) in enumerate(zip(ntsc, pal)):
            where = '%s #%d (NTSC-U %s)' % (what, i, n.get(va_key))
            for k in SAME_KEYS:
                if n.get(k) != p.get(k):
                    problems.append('%s: %s is %r in NTSC-U, %r in PAL' % (where, k, n.get(k), p.get(k)))
            if set(n) != set(p):
                problems.append('%s: keys differ: %s' % (where, sorted(set(n) ^ set(p))))
            if 'if' in n:
                ni, pi = n['if'], p.get('if', {})
                if ni.get('reg') != pi.get('reg') or len(ni.get('in', [])) != len(pi.get('in', [])):
                    problems.append('%s: the "if" differs' % where)
            nva = (n.get(va_key) or '').upper().replace('0X', '0x')
            if _note_va(p.get('note')) != nva.upper():
                problems.append('%s: the PAL note does not start with "(NTSC-U %s)"' % (where, n.get(va_key)))
            if nva and p.get(va_key, '').upper() == nva.upper():
                problems.append('%s: the PAL address is the NTSC-U one' % where)

    pair('patches', _load('config/patches.json')['patches'], _load('config/pal/patches.json')['patches'], 'va')
    pair('seed_functions', _load('config/seed_functions.json'), _load('config/pal/seed_functions.json'), 'start')
    pair('spin_waits', _load('config/spin_waits.json')['sites'], _load('config/pal/spin_waits.json')['sites'], 'va')


def strip_c(s):
    """Comments and string/char literals blanked, line structure kept."""
    s = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), s, flags=re.S)
    s = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), s)
    s = re.sub(r'"(?:\\.|[^"\\\n])*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', s)
    s = re.sub(r"'(?:\\.|[^'\\\n])+'", lambda m: "'" + ' ' * (len(m.group(0)) - 2) + "'", s)
    return s


_WRAP_DEF = re.compile(r'^void (sub_([0-9A-Fa-f]{8}))\(void\)\s*\{\s*(\w+)\(sub_[0-9A-Fa-f]{8}_gen\);\s*\}(.*)$', re.M)


def check_wraps(problems):
    def defs(rel):
        with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
            return [(m.group(1), m.group(3), m.group(4)) for m in _WRAP_DEF.finditer(f.read())]
    ntsc = {name: handler for name, handler, _ in defs('src/game/recomp/recomp_manual.c')}
    pal = {}
    for name, handler, rest in defs('src/game/recomp/recomp_manual_pal.c'):
        m = re.search(r'NTSC-U (sub_[0-9A-Fa-f]{8})', rest)
        if not m:
            problems.append('recomp_manual_pal.c: %s does not name its NTSC-U function' % name)
            continue
        pal[m.group(1)] = (name, handler)
    for name, handler in ntsc.items():
        if name not in pal:
            problems.append('recomp_manual.c wraps %s; recomp_manual_pal.c has no twin for it' % name)
        elif pal[name][1] != handler:
            problems.append('%s: handler %s in NTSC-U, %s in PAL' % (name, handler, pal[name][1]))
        elif pal[name][0] == name:
            problems.append('%s: the PAL twin has the NTSC-U address' % name)
    for name in pal:
        if name not in ntsc:
            problems.append('recomp_manual_pal.c names NTSC-U %s, which recomp_manual.c does not wrap' % name)


_NUM = re.compile(r'\b0[xX]([0-9A-Fa-f]+)[uUlL]*\b')
_SUB = re.compile(r'\bsub_([0-9A-Fa-f]{8})\b')
_PAIR = re.compile(r'\bBO3_(VA|SUB)\s*\(')


def _pair_spans(code):
    """(start, end, kind, ntsc, pal) of each BO3_VA( ... ) / BO3_SUB( ... )."""
    out = []
    for m in _PAIR.finditer(code):
        depth, i = 1, m.end()
        while i < len(code) and depth:
            depth += {'(': 1, ')': -1}.get(code[i], 0)
            i += 1
        inner = code[m.end():i - 1]
        parts = [x.strip() for x in inner.split(',')]
        out.append((m.start(), i, m.group(1), parts))
    return out


def _value(tok):
    m = re.fullmatch(r'0[xX]([0-9A-Fa-f]+)[uUlL]*', tok)
    if m:
        return int(m.group(1), 16)
    m = re.fullmatch(r'sub_([0-9A-Fa-f]{8})', tok)
    return int(m.group(1), 16) if m else None


def check_c(problems, files=None):
    if files is None:
        files = []
        for dp, dn, fn in os.walk(os.path.join(ROOT, 'src', 'game')):
            dn[:] = [d for d in dn if d not in ('gen',) and not d.startswith('gen.')]
            files += [os.path.join(dp, f) for f in fn if f.endswith(('.c', '.h'))]
    pairs = {}
    for path in sorted(files):
        rel = os.path.relpath(path, ROOT)
        if rel.replace(os.sep, '/').endswith(('recomp/recomp_manual.c', 'recomp/recomp_manual_pal.c', '/region.h')):
            continue                    # region-specific by file (check_wraps), or the macros
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
        lines = text.split('\n')
        code = strip_c(text)
        spans = _pair_spans(code)
        starts = [0]
        for l in code.split('\n'):
            starts.append(starts[-1] + len(l) + 1)

        def line_of(pos):
            lo, hi = 0, len(starts) - 1
            while lo < hi - 1:
                mid = (lo + hi) // 2
                lo, hi = (mid, hi) if starts[mid] <= pos else (lo, mid)
            return lo

        for st, en, kind, parts in spans:
            ln = line_of(st)
            if len(parts) != 2:
                problems.append('%s:%d: BO3_%s wants two values' % (rel, ln + 1, kind))
                continue
            a, b = _value(parts[0]), _value(parts[1])
            if a is None or b is None:
                problems.append('%s:%d: BO3_%s(%s) is not two literals' % (rel, ln + 1, kind, ', '.join(parts)))
                continue
            if a == b and SAME_OK not in lines[ln]:
                problems.append('%s:%d: BO3_%s(%s, %s): the PAL value is the NTSC-U one '
                                '(say "%s" on the line if that is right)' % (rel, ln + 1, kind, parts[0], parts[1], SAME_OK))
            if kind == 'VA' and not (LO <= a < HI + 1 and LO <= b < HI + 1):
                problems.append('%s:%d: BO3_VA(%s, %s) is not a guest address' % (rel, ln + 1, parts[0], parts[1]))
            prev = pairs.setdefault((kind, a), (b, rel, ln + 1))
            if prev[0] != b:
                problems.append('%s:%d: NTSC-U %#x is PAL %#x here, %#x at %s:%d'
                                % (rel, ln + 1, a, b, prev[0], prev[1], prev[2]))

        def inside(pos):
            return any(st <= pos < en for st, en, _, _ in spans)

        for m in _NUM.finditer(code):
            v = int(m.group(1), 16)
            if LO <= v < HI and not inside(m.start()):
                ln = line_of(m.start())
                if NOT_ADDR not in lines[ln]:
                    problems.append('%s:%d: %s outside BO3_VA (or say "%s" on the line)'
                                    % (rel, ln + 1, m.group(0), NOT_ADDR))
        for m in _SUB.finditer(code):
            if not inside(m.start()):
                ln = line_of(m.start())
                problems.append('%s:%d: %s outside BO3_SUB' % (rel, ln + 1, m.group(0)))


def main():
    problems = []
    check_config(problems)
    check_wraps(problems)
    check_c(problems)
    for p in problems:
        print('!!', p)
    print('region check: %s' % ('%d problem(s)' % len(problems) if problems else 'NTSC-U and PAL in step'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
