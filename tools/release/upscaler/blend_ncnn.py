#!/usr/bin/env python3
"""Two ncnn models of one network blended weight by weight, as Real-ESRGAN's
own denoise strength does: out = a * A + (1 - a) * B (BO-311, the fast model:
realesr-general-x4v3 at denoise 0.3 = 0.3 general + 0.7 WDN).

  blend_ncnn.py <net.param> <A.bin> <B.bin> <a> <out.bin>

Convolution weights are fp16 behind their 4-byte tag, biases and PReLU slopes
fp32. The arithmetic is float32 throughout, rounded to fp16 to nearest even,
so the result is the same file numpy makes (SHA-256 checked by the caller).
Standard library only."""
import struct
import sys


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def main():
    param, pa, pb, alpha, out = sys.argv[1:6]
    A = open(pa, 'rb').read()
    B = open(pb, 'rb').read()
    if len(A) != len(B):
        sys.exit('the two weight files differ in size')
    a = f32(float(alpha))
    b = f32(1.0 - float(alpha))
    res = bytearray()
    o = 0

    def mix_f32(n):
        nonlocal o
        xs = struct.unpack_from('<%df' % n, A, o)
        ys = struct.unpack_from('<%df' % n, B, o)
        res.extend(struct.pack('<%df' % n, *[f32(f32(a * x) + f32(b * y)) for x, y in zip(xs, ys)]))
        o += 4 * n

    for line in open(param).read().split('\n')[2:]:
        t = line.split()
        if not t:
            continue
        kv = dict(x.split('=', 1) for x in t[1:] if '=' in x)
        if t[0] == 'Convolution':
            n = int(kv['6'])
            if struct.unpack_from('<I', A, o)[0] != 0x01306B47 or struct.unpack_from('<I', B, o)[0] != 0x01306B47:
                sys.exit('unexpected weight tag at %d' % o)
            res += A[o:o + 4]
            o += 4
            xs = struct.unpack_from('<%de' % n, A, o)
            ys = struct.unpack_from('<%de' % n, B, o)
            res.extend(struct.pack('<%de' % n, *[f32(f32(a * x) + f32(b * y)) for x, y in zip(xs, ys)]))
            o += 2 * n
            pad = (4 - (2 * n) % 4) % 4
            res += A[o:o + pad]
            o += pad
            if kv.get('5') == '1':
                mix_f32(int(kv['0']))
        elif t[0] == 'PReLU':
            mix_f32(int(kv['0']))
    if o != len(A):
        sys.exit('the weights do not match the network (%d of %d bytes)' % (o, len(A)))
    open(out, 'wb').write(bytes(res))


if __name__ == '__main__':
    main()
