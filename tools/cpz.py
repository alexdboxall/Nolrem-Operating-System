#!/usr/bin/env python3
"""CPZ: compress a whole file one 4096-byte page at a time (every page independent).

    python3 cpz.py compress   in  out.cpz
    python3 cpz.py decompress in.cpz out
    python3 cpz.py selftest

The input is zero padded up to a whole number of pages first.

FILE FORMAT (little endian)
    0   'C' 'P' 'Z' 0x00      signature + version 0
    4   uint16                number of pages in the original (padded) file
    6   2 bytes               reserved (written as zero)
    8   uint32 x pages        page table. Top 2 bits = type, bottom 30 bits = ABSOLUTE file offset
                              where that page's stored data starts:
                                  0 = stored uncompressed (4096 bytes)
                                  1 = all zero bytes       (no data at all)
                                  2 = compressed           (RLE stream, see below)
                              Every page has an offset, zero pages included (it is where the next
                              page's data starts, so the zero page's stored length is 0).
                              A page's stored length = next page's offset - its offset; the last page
                              runs to the end of the file.
    ... the stored page data, back to back, in page order.

    A page is only stored compressed if its compressed form is SMALLER than 4090 bytes.
    4090 bytes or more -> stored uncompressed.

PAGE CODEC (same as the glyph bitmap codec; the decoder stops after 4096 output bytes)
    op byte   top 2 bits = type, low 6 bits = 6 bits of "value"
    0x00-0x3F  literal : the next (value + 1) bytes are copied as they are
    0x40-0x7F  zeros   : (value + 1) zero bytes
    0x80-0xBF  repeat  : the next byte, repeated (value + 1) times
    0xC0-0xFF  prefix  : not an operation; its low 6 bits are more significant bits of the NEXT op's
                         value. Prefixes stack, first one = top bits (big endian).
                         e.g. 65 zeros = value 64 = C1 40.
NOTE the format stores no original length, so decompressing gives back the zero padded file.
"""
import struct
import sys

PAGE = 4096
RAW_LIMIT = 4090                 # compressed size >= this: store the page uncompressed
T_RAW, T_ZERO, T_COMP = 0, 1, 2
MAX_PAGES = 0xFFFF               # the page count is a uint16
MAX_OFFSET = (1 << 30) - 1       # offsets are 30 bits
SIGNATURE = b'CPZ\x00'
HEADER_SIZE = 8

# ------------------------------------------------------------------ codec (identical to typcompress.py)

LIT, ZERO, REP, PREFIX = 0x00, 0x40, 0x80, 0xC0


# ------------------------------------------------------------------ codec

def _groups(v):
    """6-bit groups of v, most significant first, at least one."""
    g = [v & 0x3F]
    v >>= 6
    while v:
        g.append(v & 0x3F)
        v >>= 6
    return g[::-1]


def _hdr_len(v):
    """Bytes needed to write value v: one op byte plus one prefix per extra 6 bits."""
    return max(1, (v.bit_length() + 5) // 6)


def _emit(op, count, out):
    g = _groups(count - 1)
    for x in g[:-1]:
        out.append(PREFIX | x)
    out.append(op | g[-1])


def encode(data):
    """Smallest possible encoding of data, in time linear in len(data).

    cost[i] = size of the best encoding of data[i:]. Two facts keep this linear while exact:
      * cost[] never increases as i grows (drop the first byte of any encoding and it is no bigger), so
        for a run the longest length allowed by a header size is always the best choice for that size;
      * a literal of k bytes costs header(k) + k, and within one header size that is "k + cost[i + k]"
        minimised over a window of k, which a monotone queue does in O(1) per position.
    Writing a value with more prefix bytes than it needs is legal, so a header size may be tried for
    short lengths too; that is only ever more expensive, so the true minimum still wins.
    """
    from collections import deque
    data = bytes(data)
    n = len(data)
    run = [1] * n                                  # run[i]: length of the run of equal bytes at i
    for i in range(n - 2, -1, -1):
        if data[i] == data[i + 1]:
            run[i] = run[i + 1] + 1
    his, t = [], 1                                 # his[t-1]: longest count a t-byte header can express
    while True:
        his.append(64 ** t)
        if 64 ** t >= n:
            break
        t += 1
    cost = [0] * (n + 1)
    choice = [None] * (n + 1)
    queues = [deque() for _ in his]
    for i in range(n - 1, -1, -1):
        j = i + 1                                  # newest literal end that becomes available
        v = j + cost[j]
        best, pick = None, None
        for t, hi in enumerate(his, 1):
            q = queues[t - 1]
            while q and q[-1][1] > v:              # strictly worse and expires sooner: useless
                q.pop()
            q.append((j, v))
            while q[0][0] > i + hi:                # literal would be longer than this header allows
                q.popleft()
            jj, vv = q[0]                          # on equal cost this is the longest literal
            c = t + vv - i                         # t header bytes + (jj - i) literal bytes + cost[jj]
            if best is None or c <= best:
                best, pick = c, (LIT, jj - i)
        zero = data[i] == 0
        for t, hi in enumerate(his, 1):            # runs win ties: they are cheaper to decode
            k = min(run[i], hi)
            c = t + (0 if zero else 1) + cost[i + k]
            if c <= best:
                best, pick = c, (ZERO if zero else REP, k)
        cost[i], choice[i] = best, pick
    out = bytearray()
    i = 0
    while i < n:
        op, k = choice[i]
        _emit(op, k, out)
        if op == LIT:
            out += data[i:i + k]
        elif op == REP:
            out.append(data[i])
        i += k
    assert len(out) == cost[0], 'encoder cost bookkeeping is inconsistent'
    return bytes(out)


def decode(buf, pos, expected):
    """Decode one bitmap of `expected` bytes starting at buf[pos]. Returns (bytes, bytes_consumed)."""
    out = bytearray()
    start = pos
    acc = 0
    while len(out) < expected:
        if pos >= len(buf):
            raise ValueError('compressed bitmap is truncated')
        b = buf[pos]
        pos += 1
        acc = (acc << 6) | (b & 0x3F)
        if acc > expected:
            raise ValueError('run length is longer than the bitmap')
        top = b & 0xC0
        if top == PREFIX:
            continue
        count, acc = acc + 1, 0
        if len(out) + count > expected:
            raise ValueError('operation runs past the end of the bitmap')
        if top == LIT:
            if pos + count > len(buf):
                raise ValueError('literal runs past the end of the data')
            out += buf[pos:pos + count]
            pos += count
        elif top == ZERO:
            out += bytes(count)
        else:
            if pos >= len(buf):
                raise ValueError('repeat is missing its byte')
            out += bytes([buf[pos]]) * count
            pos += 1
    return bytes(out), pos - start


# ------------------------------------------------------------------ container

def _page_count(nbytes):
    n = (nbytes + PAGE - 1) // PAGE
    if n > MAX_PAGES:
        raise ValueError(f'{n} pages is more than the format allows ({MAX_PAGES}, a uint16)')
    return n


def pack(data):
    """Returns (cpz_bytes, [(type, stored_length), ...])."""
    data = bytes(data)
    npages = _page_count(len(data))
    padded = data + bytes(npages * PAGE - len(data))
    pieces = []
    for i in range(npages):
        page = padded[i * PAGE:(i + 1) * PAGE]
        if page.count(0) == PAGE:
            pieces.append((T_ZERO, b''))
            continue
        c = encode(page)
        pieces.append((T_COMP, c) if len(c) < RAW_LIMIT else (T_RAW, page))
    pos = HEADER_SIZE + 4 * npages
    table = bytearray()
    for t, b in pieces:
        if pos > MAX_OFFSET:
            raise ValueError('output is too large for 30-bit offsets')
        table += struct.pack('<I', (t << 30) | pos)
        pos += len(b)
    blob = SIGNATURE + struct.pack('<H', npages) + b'\x00\x00' + bytes(table) + b''.join(b for _, b in pieces)
    return blob, [(t, len(b)) for t, b in pieces]


def unpack(blob):
    """Returns the zero padded original file."""
    if len(blob) < HEADER_SIZE or blob[:4] != SIGNATURE:
        raise ValueError("not a CPZ file (needs the signature 'CPZ' followed by a 0 byte)")
    npages = struct.unpack_from('<H', blob, 4)[0]
    table_end = HEADER_SIZE + 4 * npages
    if table_end > len(blob):
        raise ValueError('page table runs past the end of the file')
    entries = [struct.unpack_from('<I', blob, HEADER_SIZE + 4 * i)[0] for i in range(npages)]
    view = memoryview(blob)
    out = bytearray()
    for i, e in enumerate(entries):
        t, off = e >> 30, e & MAX_OFFSET
        end = (entries[i + 1] & MAX_OFFSET) if i + 1 < npages else len(blob)
        if off < table_end or end < off or end > len(blob):
            raise ValueError(f'page {i}: offsets are out of order or outside the file')
        n = end - off
        if t == T_ZERO:
            if n:
                raise ValueError(f'page {i}: a zero page must not have stored data')
            out += bytes(PAGE)
        elif t == T_RAW:
            if n != PAGE:
                raise ValueError(f'page {i}: uncompressed page is {n} bytes, not {PAGE}')
            out += view[off:end]
        elif t == T_COMP:
            page, used = decode(view[:end], off, PAGE)      # cannot read past this page's data
            if used != n:
                raise ValueError(f'page {i}: compressed stream is {used} bytes but the table says {n}')
            out += page
        else:
            raise ValueError(f'page {i}: unknown page type {t}')
    return bytes(out)


TYPE_NAMES = {T_RAW: 'uncompressed', T_ZERO: 'zero', T_COMP: 'compressed'}


def report(src_len, out, info):
    print(f'{"page":>4}  {"type":<12} {"offset":>7} {"stored":>7}')
    pos = HEADER_SIZE + 4 * len(info)
    for i, (t, n) in enumerate(info):
        print(f'{i:>4}  {TYPE_NAMES[t]:<12} {pos:>7} {n:>7}')
        pos += n
    padded = len(info) * PAGE
    print(f'original {src_len} bytes, zero padded to {padded} ({len(info)} pages), '
          f'compressed file {len(out)} bytes ({100 * len(out) / max(src_len, 1):.1f}% of original)')


# ------------------------------------------------------------------ self test

def selftest():
    import random
    rng = random.Random(11)

    # --- codec (copied code, so test it where it lives)
    vectors = [(b'\x12\x34', b'\x01\x12\x34'), (bytes(5), b'\x44'), (b'\xAA' * 3, b'\x82\xAA'), (bytes(64), b'\x7F'),
               (bytes(65), b'\xC1\x40'), (b'\xAA' * 4095, b'\xFF\xBE\xAA'),
               (b'\xAA' * 4096, b'\xFF\xBF\xAA'), (b'', b'')]
    for raw, want in vectors:
        assert encode(raw) == want
        assert decode(want, 0, len(raw)) == (raw, len(want))
    for _ in range(200):
        d = b''.join((bytes(rng.randint(1, 300)), bytes([rng.randint(1, 255)]) * rng.randint(1, 300),
                      bytes(rng.randint(0, 255) for _ in range(rng.randint(0, 100))))[rng.randint(0, 2)] for _ in range(rng.randint(0, 8)))
        e = encode(d)
        assert decode(e, 0, len(d)) == (d, len(e))
    print('ok: page codec vectors and 200 random round trips')

    # --- container: bytes worked out by hand
    blob, info = pack(b'')
    assert blob == b'CPZ\x00' + b'\x00\x00' + b'\x00\x00' and info == [] and unpack(blob) == b''
    blob, info = pack(bytes(PAGE))                                   # one zero page: type 1, offset 12
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x0c\x00\x00\x40', blob.hex()
    blob, info = pack(b'\xAA' * PAGE)                                # type 2, offset 12, data FF BF AA
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x0c\x00\x00\x80' + b'\xff\xbf\xaa', blob.hex()
    blob, info = pack(b'\x01')                  # 1 byte -> padded; stream 80 01 FF 7E: "1 x 01" then 4095 zeros
    # (a 1-byte repeat and a 1-byte literal both cost 2 bytes; runs win ties, so 80 01 rather than 00 01)
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x0c\x00\x00\x80' + b'\x80\x01\xff\x7e', blob.hex()
    assert unpack(blob) == b'\x01' + bytes(PAGE - 1)
    noise = bytes(rng.randint(1, 255) for _ in range(PAGE))         # nonzero, effectively incompressible
    assert len(encode(noise)) >= RAW_LIMIT
    blob, info = pack(bytes(PAGE) + noise + bytes(PAGE))             # zero, raw, zero: offsets 20, 20, 4116
    assert struct.unpack_from('<III', blob, 8) == ((1 << 30) | 20, 20, (1 << 30) | (20 + PAGE)), struct.unpack_from('<III', blob, 8)
    assert len(blob) == 20 + PAGE + 0 and blob[20:] == noise and info == [(T_ZERO, 0), (T_RAW, PAGE), (T_ZERO, 0)]
    assert unpack(blob) == bytes(PAGE) + noise + bytes(PAGE)
    print('ok: container bytes match the hand-worked layouts (empty, zero page, tiny page, zero/raw/zero offsets)')

    # --- the 4090 rule: a page that compresses to 4089 bytes is compressed, 4090 is stored raw
    def page_with_literal(L):
        lit = bytes(1 + (i % 2) + 2 * (i % 7) for i in range(L))   # no two neighbours equal, never zero
        assert all(lit[i] != lit[i + 1] for i in range(L - 1))
        return lit + bytes(PAGE - L)
    for L, expect_len, expect_type in ((4086, 4089, T_COMP), (4087, 4090, T_RAW)):
        p = page_with_literal(L)
        assert len(encode(p)) == expect_len, (L, len(encode(p)))
        blob, info = pack(p)
        assert info == [(expect_type, expect_len if expect_type == T_COMP else PAGE)], info
        assert unpack(blob) == p
    print('ok: 4089-byte result is kept compressed, 4090-byte result is stored uncompressed')

    # --- random multi-page files round-trip (and sizes that are not a multiple of the page size)
    for n in (1, PAGE - 1, PAGE, PAGE + 1, 3 * PAGE + 17, 9 * PAGE):
        d = bytearray()
        while len(d) < n:
            k = rng.random()
            d += bytes(rng.randint(1, 6000)) if k < .3 else bytes(rng.randint(0, 255) for _ in range(rng.randint(1, 5000))) if k < .5 \
                else (b'hello world ' * rng.randint(1, 600))
        d = bytes(d[:n])
        blob, info = pack(d)
        back = unpack(blob)
        assert back[:n] == d and back[n:] == bytes(len(back) - n) and len(back) % PAGE == 0 and len(back) // PAGE == len(info)
    print('ok: random files (incl. sizes off a page boundary) round-trip; output is the zero padded input')

    # --- corrupt files are rejected, never mis-decoded
    good, ginfo = pack(bytes(PAGE) + b'\xAA' * PAGE + noise)         # pages: zero, compressed (3 bytes), raw
    assert [t for t, _ in ginfo] == [T_ZERO, T_COMP, T_RAW] and unpack(good)[PAGE:PAGE + 4] == b'\xAA' * 4

    def entry(blob, k):
        return struct.unpack_from('<I', blob, HEADER_SIZE + 4 * k)[0]

    def with_entry(blob, k, value):
        b = bytearray(blob)
        struct.pack_into('<I', b, HEADER_SIZE + 4 * k, value)
        return bytes(b)
    bad_cases = {
        'bad signature': b'XPZ' + good[3:],
        'bad version': good[:3] + b'\x01' + good[4:],
        'truncated header': good[:5],
        'table past end': good[:4] + b'\xff\xff' + good[6:12],
        'unknown page type': with_entry(good, 0, (3 << 30) | (entry(good, 0) & MAX_OFFSET)),
        'zero page that has data': with_entry(good, 1, entry(good, 1) + 1),          # page 0 now spans 1 byte
        'compressed stream shorter than the table says': with_entry(good, 2, entry(good, 2) + 1),
        'compressed stream longer than the table says': with_entry(good, 2, entry(good, 2) - 1),
        'offset points into the table': with_entry(good, 2, entry(good, 2) & ~MAX_OFFSET | 4),
        'offsets out of order': with_entry(good, 1, (2 << 30) | 9999999),
        'raw page wrong length': good[:-1],
        'compressed page marked raw': with_entry(good, 1, entry(good, 1) & MAX_OFFSET),
    }
    for name, b in bad_cases.items():
        try:
            unpack(b)
        except ValueError:
            continue
        raise SystemExit(f'accepted a corrupt file: {name}')
    print(f'ok: {len(bad_cases)} kinds of corrupt file rejected')
    try:
        _page_count(PAGE * (MAX_PAGES + 1))
        raise SystemExit('accepted too many pages')
    except ValueError:
        pass
    assert _page_count(PAGE * MAX_PAGES) == MAX_PAGES
    print('ok: 65535-page limit enforced')


if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['selftest']:
        selftest()
    elif len(args) == 3 and args[0] == 'compress':
        src = open(args[1], 'rb').read()
        try:
            out, info = pack(src)
            back = unpack(out)                                   # never write what we can't read back
        except ValueError as e:
            sys.exit(f'error: {e}')
        assert back[:len(src)] == src and not any(back[len(src):]), 'round trip failed'
        open(args[2], 'wb').write(out)
        print(f'{args[1]} -> {args[2]}')
        report(len(src), out, info)
    elif len(args) == 3 and args[0] == 'decompress':
        try:
            out = unpack(open(args[1], 'rb').read())
        except ValueError as e:
            sys.exit(f'error: {e}')
        open(args[2], 'wb').write(out)
        print(f'{args[1]} -> {args[2]}: {len(out)} bytes ({len(out) // PAGE} pages, zero padded to a page boundary)')
    else:
        sys.exit(__doc__)
