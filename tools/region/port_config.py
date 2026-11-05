"""Write config/<target>/*.json from config/*.json: every guest address in
the NTSC-U lists translated to a PAL build (tools/region/xbemap.py), each
code site checked to read the same instructions in both, and anything that
does not hold reported (exit status 1).

    python3 -m tools.region.port_config NTSC/default.xbe PAL/default.xbe
    python3 -m tools.region.port_config NTSC/default.xbe ENSV/default.xbe --target pal-ensv

The targets (TARGETS below): pal, the Europe (Fr,De,It) disc, the second
value of BO3_VA / BO3_SUB; pal-ensv, the Europe/Australia (En,Es,Nl,Sv)
disc, the third.

The notes stay as written for NTSC-U, each prefixed with the NTSC-U address
of its entry, so the two lists can be read side by side.
"""
import json, os, re, sys
from tools.region.xbemap import AddressMap

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ADDR = re.compile(r'^0x[0-9A-Fa-f]{8}$')
# Sites read by hand where the automatic check says no.
REVIEWED = {
    0x0017A765: 'the patched load reads the same; the compiler picked edi '
                'for ebx a few instructions later',
}
OK = ('matched', 'operand', 'section', 'between')
# target: (folder under config/, the value of BO3_VA/BO3_SUB it checks, its name)
TARGETS = {
    'pal': ('pal', 1, 'PAL (Europe, Fr/De/It)'),
    'pal-ensv': ('pal-ensv', 2, 'PAL (Europe/Australia, En/Es/Nl/Sv)'),
}


def main():
    args = sys.argv[1:]
    target = 'pal'
    if '--target' in args:
        i = args.index('--target')
        target = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2 or target not in TARGETS:
        sys.exit('usage: port_config NTSC.xbe PAL.xbe [--target %s]' % '|'.join(TARGETS))
    folder, column, label = TARGETS[target]
    out_dir = os.path.join(ROOT, 'config', folder)
    am = AddressMap(args[0], args[1])
    problems = []

    def tr(s, where, code):
        a = int(s, 16)
        b, how = am.translate(a)
        note = how
        if code:
            same, x, y = am.same_code(a, b)
            if not same and a not in REVIEWED:
                problems.append('%s %s -> %08X: code differs\n    ntsc %s\n    pal  %s' % (where, s, b, x, y))
        if not how.startswith(OK):
            problems.append('%s %s -> %08X: %s' % (where, s, b, how))
        return '0x%08X' % b

    os.makedirs(out_dir, exist_ok=True)

    # patches
    src = json.load(open(os.path.join(ROOT, 'config', 'patches.json')))
    out = []
    for p in src['patches']:
        q = dict(p)
        q['va'] = tr(p['va'], 'patch %s' % p.get('name', p.get('setting')), True)
        q['note'] = '(NTSC-U %s) ' % p['va'] + p.get('note', '')
        if 'if' in p:
            q['if'] = dict(p['if'])
            q['if']['in'] = [tr(x, 'patch if', False) for x in p['if']['in']]
        out.append(q)
    if target == 'pal':
        lead = ('PAL (Europe) twin of config/patches.json, written by tools/region/port_config.py: '
                'each va translated to the PAL default.xbe and checked to be the same instruction; ')
    else:
        lead = (label + ' twin of config/patches.json, written by tools/region/port_config.py: '
                'each va translated to that disc\'s default.xbe and checked to be the same instruction; ')
    about = (lead +
             'each note starts with the NTSC-U address it came from and keeps its NTSC-U addresses. '
             + src['about'])
    json.dump({'about': about, 'patches': out}, open(os.path.join(out_dir, 'patches.json'), 'w'), indent=2)
    print('patches:', len(out))

    # seeds
    src = json.load(open(os.path.join(ROOT, 'config', 'seed_functions.json')))
    out = []
    for e in src:
        q = dict(e); q['start'] = tr(e['start'], 'seed', True)
        q['note'] = '(NTSC-U %s) ' % e['start'] + e.get('note', '')
        out.append(q)
    json.dump(out, open(os.path.join(out_dir, 'seed_functions.json'), 'w'), indent=1)
    print('seeds:', len(out))

    # spin waits
    src = json.load(open(os.path.join(ROOT, 'config', 'spin_waits.json')))
    q = dict(src); q['sites'] = []
    for e in src['sites']:
        r = dict(e); r['va'] = tr(e['va'], 'spin', True)
        r['note'] = '(NTSC-U %s) ' % e['va'] + e.get('note', '')
        q['sites'].append(r)
    json.dump(q, open(os.path.join(out_dir, 'spin_waits.json'), 'w'), indent=2)
    print('spin waits:', len(q['sites']))

    # memory ordering (release stores; the spin-exit fence needs no address)
    src = json.load(open(os.path.join(ROOT, 'config', 'mem_order.json')))
    q = dict(src); q['release_stores'] = []
    for e in src['release_stores']:
        r = dict(e); r['va'] = tr(e['va'], 'release store', True)
        r['note'] = '(NTSC-U %s) ' % e['va'] + e.get('note', '')
        q['release_stores'].append(r)
    json.dump(q, open(os.path.join(out_dir, 'mem_order.json'), 'w'), indent=2)
    print('release stores:', len(q['release_stores']))

    # the C: every BO3_VA / BO3_SUB pair is the map's translation
    from tools.region import check
    n = 0
    for dp, dn, fn in os.walk(os.path.join(ROOT, 'src', 'game')):
        dn[:] = [d for d in dn if d != 'gen' and not d.startswith('gen.')]
        for f in sorted(fn):
            if not f.endswith(('.c', '.h')):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, ROOT)
            code = check.strip_c(open(path, encoding='utf-8', errors='replace').read())
            for st, en, kind, parts in check._pair_spans(code):
                if len(parts) <= column or None in (check._value(parts[0]), check._value(parts[column])):
                    continue
                a, b = check._value(parts[0]), check._value(parts[column])
                t, how = am.translate(a)
                n += 1
                if t != b:
                    problems.append('%s: BO3_%s(%s, .. %s): the map says %#x (%s)' % (rel, kind, parts[0], parts[column], t, how))
                elif am.is_code(a) and kind == 'SUB' or (kind == 'VA' and am.a.secs[am.a.index(a)][0] == '.text'):
                    same, x, y = am.same_code(a, b, 3)
                    if not same and a not in REVIEWED:
                        problems.append('%s: BO3_%s(%s, .. %s): code differs\n    ntsc %s\n    pal  %s' % (rel, kind, parts[0], parts[column], x, y))
    print('C pairs checked:', n)

    for p in problems:
        print('!!', p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
