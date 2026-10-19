"""Address map between two builds of the same title (Burnout 3 NTSC-U -> PAL).

The PAL default.xbe is the same program as the NTSC-U one, rebuilt: the
code moved by a few hundred bytes (more text, other strings), so every
guest address the port names (config/patches.json, the seed list, the C
under src/game) has a PAL twin. This finds them from the two executables
alone, with nothing of either committed:

  .text          both disassembled (capstone, linear sweep), each
                 instruction reduced to its mnemonic and operands with
                 absolute addresses blanked, the two lists aligned with
                 diff(1). A matched instruction maps byte for byte.
  .rdata .data   the same over 32-bit words.
  bss            not in the file: every absolute operand of a matched
                 instruction pair is evidence (NTSC A was PAL B), and an
                 address with no reference of its own takes the shift of
                 its neighbours when both agree.
  library code   XDK sections moved as a block (XMV .. XPP, DOLBY ...).

translate(va) returns (pal_va, how); `how` says on what it rests, and
anything not plainly matched is flagged rather than guessed silently.

Usage:  python3 -m tools.region.xbemap NTSC.xbe PAL.xbe 0x54F900 ...
"""
import bisect, collections, os, re, struct, subprocess, sys, tempfile

LO, HI = 0x11000, 0x778600
_HEX = re.compile(r'0x[0-9a-f]+')


def parse_xbe(path):
    d = open(path, 'rb').read()
    base, = struct.unpack_from('<I', d, 0x104)
    nsec, secaddr = struct.unpack_from('<II', d, 0x11c)
    secs = []
    for i in range(nsec):
        o = secaddr - base + i * 56
        fl, va, vs, ra, rs, na = struct.unpack_from('<IIIIII', d, o)
        no = na - base
        secs.append((d[no:d.index(b'\0', no)].decode(), va, vs, ra, rs))
    return d, secs


class Build:
    def __init__(self, path):
        self.data, self.secs = parse_xbe(path)

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def index(self, va):
        for i, (n, v, vs, ra, rs) in enumerate(self.secs):
            if v <= va < v + vs:
                return i
        return None

    def raw(self, va, n):
        for (nm, v, vs, ra, rs) in self.secs:
            if v <= va < v + rs:
                o = ra + va - v
                return self.data[o:o + min(n, v + rs - va)]
        return b''


def _text_rows(b, md):
    n, va, vs, ra, rs = b.sec('.text')
    code = b.data[ra:ra + rs]
    rows, toks = [], []
    for addr, size, m, ops in md.disasm_lite(code, va):
        toks.append(m + ' ' + _HEX.sub(lambda mo: 'A' if LO <= int(mo.group(0), 16) < HI else mo.group(0), ops))
        rows.append((addr, size))
    return rows, toks


def _data_rows(b, name):
    n, va, vs, ra, rs = b.sec(name)
    rows, toks = [], []
    for o in range(0, rs - 3, 4):
        w, = struct.unpack_from('<I', b.data, ra + o)
        toks.append('A' if LO <= w < HI else '%x' % w)
        rows.append((va + o, 4))
    return rows, toks


def _align(ra, ta, rb, tb):
    """(row_a, row_b, exact) for the lines diff(1) keeps or changes 1:1."""
    with tempfile.TemporaryDirectory() as t:
        fa, fb = os.path.join(t, 'a'), os.path.join(t, 'b')
        open(fa, 'w').write('\n'.join(ta) + '\n')
        open(fb, 'w').write('\n'.join(tb) + '\n')
        out = subprocess.run(['diff', fa, fb], capture_output=True, text=True).stdout
    res, i, j = [], 0, 0
    for l in out.splitlines():
        m = re.match(r'^(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?$', l)
        if not m:
            continue
        a1, a2, c = int(m[1]), int(m[2] or m[1]), m[3]
        b1, b2 = int(m[4]), int(m[5] or m[4])
        sa = a1 if c == 'a' else a1 - 1
        while i < sa:
            res.append((ra[i], rb[j], True)); i += 1; j += 1
        if c == 'c':
            na, nb = a2 - a1 + 1, b2 - b1 + 1
            for k in range(min(na, nb)):
                res.append((ra[i + k], rb[j + k], na == nb and ra[i + k][1] == rb[j + k][1] and False))
            i += na; j += nb
        elif c == 'd':
            i += a2 - a1 + 1
        else:
            j += b2 - b1 + 1
    while i < len(ra):
        res.append((ra[i], rb[j], True)); i += 1; j += 1
    return res


class AddressMap:
    def __init__(self, ntsc_path, pal_path):
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
        self.a, self.b = Build(ntsc_path), Build(pal_path)
        assert [s[0] for s in self.a.secs] == [s[0] for s in self.b.secs], 'section lists differ'
        self.m = {}
        for kind in ('.text', '.rdata', '.data'):
            if kind == '.text':
                ra, ta = _text_rows(self.a, md); rb, tb = _text_rows(self.b, md)
            else:
                ra, ta = _data_rows(self.a, kind); rb, tb = _data_rows(self.b, kind)
            for (x, sx), (y, sy), ex in _align(ra, ta, rb, tb):
                self.m[x] = (y, ex, sx)
        self.keys = sorted(self.m)
        self._evidence()

    def _evidence(self):
        """Absolute operands and branch targets of matched instruction pairs.

        A pair counts only when the instructions agree byte for byte
        before the operand; each NTSC value takes its most frequent PAL
        partner, with the count kept (self.evn) to weigh it."""
        ev = collections.defaultdict(collections.Counter)
        evb = collections.defaultdict(collections.Counter)
        for x, (y, ex, size) in self.m.items():
            if not ex or size < 4:
                continue
            p, q = self.a.raw(x, size), self.b.raw(y, size)
            if len(p) != size or len(q) != size:
                continue
            for o in range(size - 3):
                w1, = struct.unpack_from('<I', p, o); w2, = struct.unpack_from('<I', q, o)
                if LO <= w1 < HI and LO <= w2 < HI and p[:o] == q[:o] and -0x4000 < w2 - w1 < 0x4000:
                    ev[w1][w2] += 1
            # call/jmp/jcc rel32: the targets pair up too
            op = p[0]
            o = 1 if op in (0xE8, 0xE9) else (2 if op == 0x0F and 0x80 <= p[1] <= 0x8F else 0)
            if o and size == o + 4 and p[:o] == q[:o]:
                t1 = x + size + struct.unpack_from('<i', p, o)[0]
                t2 = y + size + struct.unpack_from('<i', q, o)[0]
                if LO <= t1 < HI and LO <= t2 < HI:
                    evb[t1][t2] += 1
        # Code is located by branch targets only: a text address seen as an
        # absolute operand is as often a constant that merely looks like one.
        # Data by absolute operands.
        tx = self.a.sec('.text')
        self.ev, self.evn = {}, {}
        for k, v in ev.items():
            if not (tx[1] <= k < tx[1] + tx[2]):
                (b, c), = v.most_common(1)
                self.ev[k], self.evn[k] = b, c
        for k, v in evb.items():
            (b, c), = v.most_common(1)
            self.ev[k], self.evn[k] = b, c
        self.evk = sorted(self.ev)

    def translate(self, va):
        i = self.a.index(va)
        if i is None:
            return va, 'outside'
        n, v, vs, ra, rs = self.a.secs[i]
        pv = self.b.secs[i][1]
        if n not in ('.text', '.rdata', '.data'):
            return va + (pv - v), 'section'
        e = self.ev.get(va)
        r = None
        if va < v + rs:
            k = self.keys[bisect.bisect_right(self.keys, va) - 1]
            y, ex, size = self.m[k]
            if k <= va < k + size and self.a.index(k) == i and ex:
                r = y + (va - k)
        if r is not None and (e is None or e == r):
            return r, 'matched' + ('+ref' if e is not None else '')
        if e is not None:
            # references outvote the alignment: in code a moved function,
            # in data the word diff (whose addresses all read alike)
            return e, 'operand' + ('' if r is None else '(align said %#x)' % r)
        j = bisect.bisect_left(self.evk, va)
        lo = self.evk[j - 1] if j > 0 else None
        hi = self.evk[j] if j < len(self.evk) else None
        dl = self.ev[lo] - lo if lo is not None else None
        dh = self.ev[hi] - hi if hi is not None else None
        if r is not None and n == '.text':
            return r, 'matched'
        if dl is not None and dl == dh and (r is None or r == va + dl):
            return va + dl, 'between(%#x..%#x)' % (lo, hi)
        if r is not None:
            return r, 'UNSURE(align %#x, refs %#x:%s %#x:%s)' % (r, lo or 0, dl and hex(dl), hi or 0, dh and hex(dh))
        return va + (dl or 0), 'UNSURE(%#x:%s %#x:%s)' % (lo or 0, dl and hex(dl), hi or 0, dh and hex(dh))

    def same_code(self, va, pva, n=6):
        """Do the n instructions from va (NTSC) and pva (PAL) read the same?
        Absolute operands and branch targets must be each other's
        translation (where the map knows them), not merely both addresses."""
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        def ins(build, a):
            out = []
            for addr, size, m, ops in md.disasm_lite(build.raw(a, 96), a):
                out.append((m, ops))
                if len(out) == n:
                    break
            return out
        x, y = ins(self.a, va), ins(self.b, pva)
        if len(x) != n or len(y) != n:
            return False, x, y
        for (m1, o1), (m2, o2) in zip(x, y):
            if m1 != m2:
                return False, x, y
            h1, h2 = _HEX.findall(o1), _HEX.findall(o2)
            if _HEX.sub('#', o1) != _HEX.sub('#', o2) or len(h1) != len(h2):
                return False, x, y
            for a, b in zip(h1, h2):
                a, b = int(a, 16), int(b, 16)
                if a == b:
                    continue
                if LO <= a < HI:
                    t, how = self.translate(a)
                    if t != b and not how.startswith('UNSURE'):
                        return False, x, y
                elif a != b:
                    return False, x, y
        return True, x, y

    def is_code(self, va):
        i = self.a.index(va)
        return i is not None and self.a.secs[i][0] not in ('.rdata', '.data', '.data1', 'XON_RD', '$$XTIMAGE', '$$XSIMAGE', '.XTLID')


if __name__ == '__main__':
    am = AddressMap(sys.argv[1], sys.argv[2])
    for x in sys.argv[3:]:
        a = int(x, 16); b, how = am.translate(a)
        extra = ''
        if am.is_code(a):
            ok, _, _ = am.same_code(a, b)
            extra = ' code=' + ('same' if ok else 'DIFFERENT')
        print('%08X -> %08X  %s%s' % (a, b, how, extra))
