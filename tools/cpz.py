#!/usr/bin/env python3
"""CPZ: compress a whole file one 4096-byte page at a time (every page independent).

    python3 cpz.py compress   in  out.cpz [--no-dict] [--effort 0|1|2] [-j N]
    python3 cpz.py decompress in.cpz out
    python3 cpz.py selftest

The input is zero padded up to a whole number of pages first.

FILE FORMAT (little endian)
    0   'C' 'P' 'Z' 0x00      signature + version 0
    4   uint16                number of pages in the original (padded) file
    6   2 bytes               reserved (written as zero)
    8   uint32 x (pages+1)    page table. Top 3 bits = type, bottom 29 bits = ABSOLUTE file offset
                              where that page's stored data starts:
                                  000 = stored uncompressed (4096 bytes)
                                  001 = dictionary mode     (stream, see DICTIONARY CODEC below)
                                  010 = all zero bytes      (no data at all)
                                  100 = RLE mode            (stream, see RLE CODEC below)
                                  110 = end marker (last entry only)
                              (011, 101 and 111 are not used.) The three types that existed before the
                              dictionary mode keep the bit patterns they always had: they used to be a
                              2 bit field 00 / 01 / 10 / 11 followed by a 30 bit offset, and for any offset
                              under 512 MB that is exactly this 3 bit field with a low bit of 0.
                              Every page has an offset, zero pages included (it is where the next
                              page's data starts, so the zero page's stored length is 0).
                              The LAST entry is an end marker, type 110, whose offset is where the stored
                              data ends (= the file size when written). So a page's stored length is
                              always: next entry's offset - its offset, the last page included.
    ... the stored page data, back to back, in page order.

    Each page picks whichever of RLE / dictionary / zero is smallest (on a tie, RLE), and is only stored
    compressed if that is SMALLER than 4090 bytes; 4090 bytes or more -> stored uncompressed.

RLE CODEC (same as the glyph bitmap codec; the decoder stops after 4096 output bytes)
    op byte   top 2 bits = type, low 6 bits = 6 bits of "value"
    0x00-0x3F  literal : the next (value + 1) bytes are copied as they are
    0x40-0x7F  zeros   : (value + 1) zero bytes
    0x80-0xBF  repeat  : the next byte, repeated (value + 1) times
    0xC0-0xFF  prefix  : not an operation; its low 6 bits are more significant bits of the NEXT op's
                         value. Prefixes stack, first one = top bits (big endian).
                         e.g. 65 zeros = value 64 = C1 40.

DICTIONARY CODEC (one dictionary per page, empty at the start of the page; up to 128 entries)
    0x00-0x0F  add     : the next (N + 2) bytes are output as they are AND become the next dictionary
                         entry (entries are numbered 0, 1, 2 ... in the order they are added; they can
                         never be removed). Only allowed while the dictionary has fewer than 128 entries.
                         Entries are therefore 2 to 17 bytes long. A prefix cannot extend this op.
    0x10-0x1F  literal : the next (N + 1) bytes are output as they are, not added to the dictionary
    0x20-0x2F  zeros   : (N + 2) zero bytes
    0x30-0x3F  prefix  : not an operation; its low 4 bits are more significant bits of the NEXT op's N,
                         for a 0x10-0x1F, 0x20-0x2F or 0x40-0x4F op only. Prefixes stack, first one = top bits.
                         e.g. 18 zeros = N 16 = 31 20; 17 literal bytes = N 16 = 31 10.
    0x40-0x4F  repeat  : the single byte that follows the op is output (N + 2) times. (prefixes allowed)
    0x50-0x7F  zeros   : (op - 0x50 + 18) zero bytes, i.e. 18 to 65, in one byte. (no prefix)
    0x80-0xFF  entry   : output dictionary entry (op - 0x80). It must already have been added.
    The decoder stops after 4096 output bytes, and the stream must end exactly there.

NOTE the format stores no original length, so decompressing gives back the zero padded file.
"""
import struct
import sys

PAGE = 4096
RAW_LIMIT = 4090                 # compressed size >= this: store the page uncompressed
T_RAW, T_DICT, T_ZERO, T_COMP, T_END = 0b000, 0b001, 0b010, 0b100, 0b110    # T_COMP = the RLE mode
TYPE_SHIFT = 29                  # table entry = type << 29 | offset
MAX_PAGES = 0xFFFF               # the page count is a uint16
MAX_OFFSET = (1 << TYPE_SHIFT) - 1   # offsets are 29 bits
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


# ------------------------------------------------------------------ dictionary codec

D_ADD, D_LIT, D_ZERO, D_PREFIX, D_REPEAT, D_ZLONG, D_REF = 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x80
DICT_MAX = 128                   # entries 0..127 are the ops 0x80..0xFF
DICT_ENTRY_MAX = 17              # an add op holds N + 2 bytes, N <= 15
ZLONG_MIN, ZLONG_MAX = 18, 65    # what the one byte zero run ops 0x50-0x7F express
DICT_INF = 1 << 40


def _nibbles(v):
    """4-bit groups of v, most significant first, at least one."""
    g = [v & 15]
    v >>= 4
    while v:
        g.append(v & 15)
        v >>= 4
    return g[::-1]


def _run_header(kind, count):
    """Bytes of header (prefixes + op) for a run: 'L' literal (count >= 1), 'Z' zeros or 'P' repeat (count >= 2)."""
    if kind == 'L':
        return len(_nibbles(count - 1))
    if kind == 'Z' and count <= ZLONG_MAX:
        return 1
    return len(_nibbles(count - 2))


def _dict_emit(kind, count, out):
    """Header of a literal ('L'), zero run ('Z') or repeat ('P') of `count` bytes: prefixes for the high bits, then the op."""
    if kind == 'Z' and ZLONG_MIN <= count <= ZLONG_MAX:
        out.append(D_ZLONG + count - ZLONG_MIN)
        return
    g = _nibbles(count - (1 if kind == 'L' else 2))
    for x in g[:-1]:
        out.append(D_PREFIX | x)
    out.append({'L': D_LIT, 'Z': D_ZERO, 'P': D_REPEAT}[kind] | g[-1])


def dict_decode(buf, pos, expected):
    """Decode one page of `expected` bytes of dictionary mode starting at buf[pos]. Returns (bytes, bytes_consumed).
    Strict: anything the format doesn't allow is an error, not a guess."""
    out = bytearray()
    start = pos
    entries = []
    acc, prefixed = 0, False
    while len(out) < expected:
        if pos >= len(buf):
            raise ValueError('dictionary stream is truncated')
        b = buf[pos]
        pos += 1
        top = b >> 4
        if b >= D_REF:                                        # 0x80-0xFF: a dictionary entry
            if prefixed:
                raise ValueError('a prefix must be followed by a literal, a zero run or a repeat')
            k = b - D_REF
            if k >= len(entries):
                raise ValueError(f'dictionary entry {k} is used before it is added')
            if len(out) + len(entries[k]) > expected:
                raise ValueError('dictionary entry runs past the end of the page')
            out += entries[k]
            continue
        if top == 3:                                          # 0x30-0x3F: prefix
            if acc > expected:
                raise ValueError('run length is longer than the page')
            acc = (acc << 4) | (b & 15)
            prefixed = True
            continue
        if top >= 5:                                          # 0x50-0x7F: 18..65 zeros in one byte
            if prefixed:
                raise ValueError('a prefix cannot extend a one byte zero run')
            count = b - D_ZLONG + ZLONG_MIN
            if len(out) + count > expected:
                raise ValueError('operation runs past the end of the page')
            out += bytes(count)
            continue
        if top == 0:                                          # 0x00-0x0F: literal that is also added
            if prefixed:
                raise ValueError('a prefix cannot extend an add-to-dictionary literal')
            if len(entries) >= DICT_MAX:
                raise ValueError(f'dictionary already has {DICT_MAX} entries: no more can be added')
        count = ((acc << 4) | (b & 15)) + (1 if top == 1 else 2)
        acc, prefixed = 0, False
        if len(out) + count > expected:
            raise ValueError('operation runs past the end of the page')
        if top == 2:                                          # 0x20-0x2F: zeros
            out += bytes(count)
            continue
        if top == 4:                                          # 0x40-0x4F: the next byte, repeated
            if pos >= len(buf):
                raise ValueError('repeat has no byte to repeat')
            out += bytes((buf[pos],)) * count
            pos += 1
            continue
        if pos + count > len(buf):                            # 0x00-0x0F and 0x10-0x1F: literal bytes follow
            raise ValueError('literal runs past the end of the data')
        chunk = bytes(buf[pos:pos + count])
        pos += count
        out += chunk
        if top == 0:
            entries.append(chunk)
    return bytes(out), pos - start


def _tiers(n, first, one):
    """(header bytes, shortest, longest) count a run header of that size can express, for counts first..n.
    The one byte form reaches `one`; every extra prefix nibble after that makes N 4 bits longer
    (N + first is the count, N < 16**t)."""
    out, t, lo, hi = [], 1, first, one
    while lo <= n:
        out.append((t, lo, min(hi, n)))
        lo, t = hi + 1, t + 1
        hi = 16 ** t - 1 + first
    return out


def _parse(data, refs_in, forced_in, lb, scale):
    """Cheapest way to cover data with literals, zero runs, repeats, dictionary references and FORCED definitions.

    Costs are in 1/scale bytes. Plain literals (1+ bytes), zero runs and repeats of a non-zero byte (2+ bytes) are
    free choices of any length; the others come from the caller:
      refs_in[i]   = [(start, length, entry, cost)]  references that end at i (the caller decides what they cost
                     and makes sure they are only offered where the entry has been defined by then)
      forced_in[i] = [(start, length, entry)]        definitions (add ops) ending at i. They MUST appear in the
                     result, so nothing else may overlap them
      lb[i]        = end of the last forced definition that starts before i: a segment ending at i can only
                     start at or after that, or it would overlap one.
    Linear time: a literal costs (count + header) and, within one header size, "count + cost" over a window of
    starts is a sliding-window minimum, kept in a monotone queue. Zero runs and repeats are the same with a window
    that also can't reach back past the start of the run of equal bytes. (The three kinds switch header size at
    different counts, so each has its own tiers.)
    Returns [(kind, start, end, entry)], kind 'L' literal, 'Z' zeros, 'P' repeat, 'R' reference, 'D' definition.
    """
    from collections import deque
    n = len(data)
    ltiers, ztiers, ptiers = _tiers(n, 1, 16), _tiers(n, 2, ZLONG_MAX), _tiers(n, 2, 17)
    dp = [DICT_INF] * (n + 1)
    dp[0] = 0
    back = [None] * (n + 1)
    run = [0] * (n + 1)                            # length of the run of equal bytes ending at i
    for i in range(1, n + 1):
        run[i] = run[i - 1] + 1 if i > 1 and data[i - 1] == data[i - 2] else 1
    lq = [deque() for _ in ltiers]                 # (dp[j] - j*scale, j): literals starting at j
    zq = [deque() for _ in ztiers]                 # (dp[j], j): zero runs starting at j
    pq = [deque() for _ in ptiers]                 # (dp[j], j): repeats starting at j
    for i in range(1, n + 1):
        best, pick = DICT_INF, None
        for ti, (t, lo, hi) in enumerate(ltiers):
            j = i - lo                             # the newest start this header size can reach
            if j >= 0 and dp[j] < DICT_INF:
                kv = dp[j] - j * scale
                q = lq[ti]
                while q and q[-1][0] >= kv:
                    q.pop()
                q.append((kv, j))
            q = lq[ti]
            left = max(i - hi, lb[i])
            while q and q[0][1] < left:
                q.popleft()
            if q:
                c = q[0][0] + (i + t) * scale
                if c < best:
                    best, pick = c, ('L', q[0][1], None)
        zero = data[i - 1] == 0
        for tiers, queues, kind in ((ztiers, zq, 'Z'), (ptiers, pq, 'P')):
            for ti, (t, lo, hi) in enumerate(tiers):
                j = i - lo
                if j >= 0 and dp[j] < DICT_INF:
                    q = queues[ti]
                    while q and q[-1][0] >= dp[j]:
                        q.pop()
                    q.append((dp[j], j))
                if run[i] >= lo and zero == (kind == 'Z'):
                    left = max(i - hi, lb[i], i - run[i])
                    q = queues[ti]
                    while q and q[0][1] < left:
                        q.popleft()
                    if q:
                        c = q[0][0] + (t + (kind == 'P')) * scale
                        if c <= best:              # on a tie prefer the run
                            best, pick = c, (kind, q[0][1], None)
        for j, length, entry, cost in refs_in[i]:
            if dp[j] < DICT_INF and dp[j] + cost < best:
                best, pick = dp[j] + cost, ('R', j, entry)
        for j, length, entry in forced_in[i]:
            if dp[j] < DICT_INF and dp[j] + (1 + length) * scale < best:
                best, pick = dp[j] + (1 + length) * scale, ('D', j, entry)
        dp[i], back[i] = best, pick
    if dp[n] >= DICT_INF:
        return None, None
    segs, i = [], n
    while i > 0:
        kind, j, entry = back[i]
        segs.append((kind, j, i, entry))
        i = j
    return segs[::-1], dp[n]


def _no_forced(n):
    return [[] for _ in range(n + 1)], [0] * (n + 1)


def _forced_tables(n, forced):
    """forced = {entry: start}. Returns (forced_in, lb) for _parse."""
    forced_in = [[] for _ in range(n + 1)]
    end_of = {}
    for entry, a in forced.items():
        forced_in[a + len(entry)].append((a, len(entry), entry))
        end_of[a] = a + len(entry)
    lb, cur = [0] * (n + 1), 0
    for i in range(n + 1):
        if i - 1 in end_of:
            cur = max(cur, end_of[i - 1])
        lb[i] = cur
    return forced_in, lb


def _stream(data, segs):
    """Writes the dictionary-mode stream for a parse. Dictionary entries are numbered in the order they are added."""
    out = bytearray()
    index = {}
    for kind, a, b, entry in segs:
        if kind == 'L':
            _dict_emit('L', b - a, out)
            out += data[a:b]
        elif kind == 'Z':
            _dict_emit('Z', b - a, out)
        elif kind == 'P':
            _dict_emit('P', b - a, out)
            out.append(data[a])
        elif kind == 'D':
            assert 2 <= b - a <= DICT_ENTRY_MAX and len(index) < DICT_MAX and entry not in index
            out.append(D_ADD | (b - a - 2))
            out += data[a:b]
            index[entry] = len(index)
        else:
            out.append(D_REF + index[entry])
    return bytes(out)


def _parse_cost(segs):
    """Size in bytes of the stream for a parse."""
    total = 0
    for kind, a, b, entry in segs:
        count = b - a
        if kind == 'L':
            total += count + _run_header('L', count)
        elif kind in ('Z', 'P'):
            total += _run_header(kind, count) + (kind == 'P')
        elif kind == 'R':
            total += 1
        else:
            total += 1 + count
    return total


def _without_entry(segs, entry):
    """The same covering with one entry gone: its definition and references become literal bytes, merged into any
    literal next to them. It is a valid parse of the page without that entry, so it is never smaller than the best
    one, and when it is no bigger than the parse WITH the entry, the entry is not earning its place."""
    out = []
    for kind, a, b, e in segs:
        if e == entry and kind in ('D', 'R'):
            kind, e = 'L', None
        if kind == 'L' and out and out[-1][0] == 'L':
            out[-1] = ('L', out[-1][1], b, None)
        else:
            out.append((kind, a, b, e))
    return out


def _candidates(data):
    """{string: (all start positions, how many times it occurs without overlapping itself)} for every 2..17 byte
    string that isn't all zeros (a zero run is already as cheap as a reference) and occurs at least twice."""
    n = len(data)
    found = {}
    for length in range(2, DICT_ENTRY_MAX + 1):
        where = {}
        for j in range(n - length + 1):
            where.setdefault(data[j:j + length], []).append(j)
        for s, ps in where.items():
            if len(ps) < 2 or not any(s):
                continue
            count, end = 0, -1
            for p in ps:
                if p >= end:
                    count, end = count + 1, p + length
            if count >= 2:
                found[s] = (ps, count)
    return found


def _evaluate(data, cands, chosen):
    """Exact cheapest covering of data when exactly the entries in `chosen` ({string: where it is defined}) exist:
    each is defined at that position and may be referenced from any later occurrence. (None, None) if two of the
    definitions overlap."""
    n = len(data)
    spans = sorted((a, a + len(s)) for s, a in chosen.items())
    for (a1, b1), (a2, b2) in zip(spans, spans[1:]):
        if b1 > a2:
            return None, None
    forced_in, lb = _forced_tables(n, chosen)
    refs_in = [[] for _ in range(n + 1)]
    for s, a in chosen.items():
        for p in cands[s][0]:
            if p >= a + len(s) and p >= lb[p + len(s)]:
                refs_in[p + len(s)].append((p, len(s), s, 1))
    return _parse(data, refs_in, forced_in, lb, 1)


def _refine(data, cands, chosen, effort):
    """Local search over which entries exist, judged by the exact size of the best covering (_evaluate).
    effort 1: try dropping each entry, keep the drop if the page doesn't get bigger; repeat until nothing changes.
    effort 2: also try defining the most promising missing candidates and moving each definition to another
              occurrence, within a fixed budget of evaluations (so the result never depends on the speed of the machine).
    Returns (chosen, segs, cost)."""
    chosen = dict(chosen)
    segs, cur = _evaluate(data, cands, chosen)
    evals, budget = 0, 800
    improved = True
    while improved:
        improved = False
        for s in list(chosen):
            trial = {t: a for t, a in chosen.items() if t != s}
            tsegs, c = _evaluate(data, cands, trial)
            if c is not None and c <= cur:
                improved |= c < cur
                chosen, segs, cur = trial, tsegs, c
        if effort < 2:
            continue
        pool = sorted((s for s in cands if s not in chosen), key=lambda s: (cands[s][1] - 1) * (len(s) - 1), reverse=True)[:80]
        moves = [(s, a) for s in pool for a in cands[s][0][:3]] + [(s, a) for s in list(chosen) for a in cands[s][0][:6]]
        for s, a in moves:
            if evals >= budget:
                break
            if (s in chosen and chosen[s] == a) or (s not in chosen and len(chosen) >= DICT_MAX):
                continue
            trial = dict(chosen)
            trial[s] = a
            evals += 1
            tsegs, c = _evaluate(data, cands, trial)
            if c is not None and c < cur:
                chosen, segs, cur, improved = trial, tsegs, c, True
        if evals >= budget:
            break
    return chosen, segs, cur


def dict_encode(data, overhead=(8, 16, 24), effort=1):
    """A small dictionary-mode encoding of data (not provably the smallest: choosing which strings to define is a
    much harder problem than the RLE mode's; the choice below is a heuristic, everything after it is exact).

    1. Every string of 2..17 bytes that repeats is a candidate. Cover the page with the cheapest mix of literals,
       zero runs, repeats and candidate references, charging each reference its byte plus a share of what its entry
       will cost (`overhead` bytes spread over the entry's occurrences), since the first use has to define it.
    2. The entries that parse used are the starting set, tried two ways: those used at least twice, and all of them
       (a string that repeats at a different alignment each time is used once per alignment there, yet is worth
       an entry). At most the 128 best. Each is defined at its first use in that parse; because that parse is a
       valid covering, the definitions can't overlap.
    3. Cover the page again, exactly this time: the definitions are forced, references are only allowed after the
       definition, and every cost is real. Entries nobody references are dropped and it is repeated.
    The best of the strengths in `overhead` (and of using no dictionary at all) wins.
    4. effort >= 1: local search on the winner's entries (see _refine). effort 0 skips it (about 20x faster, a few
       percent bigger); effort 2 searches harder.
    """
    data = bytes(data)
    n = len(data)
    if n == 0:
        return b''
    S = 8
    no_forced, no_lb = _no_forced(n)
    segs, cost = _parse(data, [[] for _ in range(n + 1)], no_forced, no_lb, 1)
    best = _stream(data, segs)                                   # no dictionary at all
    assert len(best) == cost
    best_chosen = None
    cands = _candidates(data)
    if cands:
        for k in overhead:
            refs_in = [[] for _ in range(n + 1)]
            for s, (ps, count) in cands.items():
                c = S + -(-k * S // count)                       # the byte, plus this entry's share of its overhead
                for p in ps:
                    refs_in[p + len(s)].append((p, len(s), s, c))
            guess, _ = _parse(data, refs_in, no_forced, no_lb, S)
            uses = {}
            for kind, a, b, s in guess:
                if kind == 'R':
                    uses.setdefault(s, []).append(a)
            tried = []
            for min_uses in (2, 1):                              # entries the guess used at least twice, or at all
                chosen = {s: u[0] for s, u in uses.items() if len(u) >= min_uses}
                if len(chosen) > DICT_MAX:
                    top = sorted(chosen, key=lambda s: (cands[s][1] - 1) * (len(s) - 1), reverse=True)[:DICT_MAX]
                    chosen = {s: chosen[s] for s in top}
                if not chosen or chosen in tried:
                    continue
                tried.append(dict(chosen))
                while chosen:
                    segs, cost = _evaluate(data, cands, chosen)
                    used = {s for kind, a, b, s in segs if kind == 'R'}
                    dead = [s for s in chosen if s not in used]
                    if not dead:                                 # none unused: drop any that don't pay for themselves
                        dead = [s for s in chosen if _parse_cost(_without_entry(segs, s)) <= cost]
                    if not dead:
                        stream = _stream(data, segs)
                        assert len(stream) == cost, 'encoder cost bookkeeping is inconsistent'
                        if len(stream) < len(best):
                            best, best_chosen = stream, dict(chosen)
                        break
                    for s in dead:
                        del chosen[s]
        if effort >= 1 and best_chosen:
            _, segs, cost = _refine(data, cands, best_chosen, effort)
            stream = _stream(data, segs)
            assert len(stream) == cost, 'encoder cost bookkeeping is inconsistent'
            if len(stream) < len(best):
                best = stream
    assert dict_decode(best, 0, n) == (data, len(best)), 'dictionary encoder produced a stream that does not decode'
    return best


# ------------------------------------------------------------------ container

def _page_count(nbytes):
    n = (nbytes + PAGE - 1) // PAGE
    if n > MAX_PAGES:
        raise ValueError(f'{n} pages is more than the format allows ({MAX_PAGES}, a uint16)')
    return n


def choose_page_mode(page, use_dict=True, effort=1):
    """Returns (type, stored_bytes) for one non-zero page: whichever of RLE and dictionary mode is smaller (RLE on
    a tie), but only if that is under RAW_LIMIT bytes; otherwise the page is stored as it is."""
    best = None
    rle = encode(page)
    if len(rle) < RAW_LIMIT:
        best = (T_COMP, rle)
    if use_dict:
        dct = dict_encode(page, effort=effort)
        if dct is not None and len(dct) < RAW_LIMIT and (best is None or len(dct) < len(best[1])):
            best = (T_DICT, dct)
    return best or (T_RAW, page)


def _page_task(args):
    page, use_dict, effort = args
    return choose_page_mode(page, use_dict, effort)


def pack(data, use_dict=True, effort=1, jobs=1):
    """Returns (cpz_bytes, [(type, stored_length), ...]). Pages are independent, so `jobs` > 1 encodes that many at a time
    (the output is identical whatever `jobs` is)."""
    data = bytes(data)
    npages = _page_count(len(data))
    padded = data + bytes(npages * PAGE - len(data))
    pages = [padded[i * PAGE:(i + 1) * PAGE] for i in range(npages)]
    todo = [i for i, pg in enumerate(pages) if pg.count(0) != PAGE]
    work = [(pages[i], use_dict, effort) for i in todo]
    if jobs > 1 and len(work) > 1:
        import multiprocessing
        with multiprocessing.Pool(min(jobs, len(work))) as pool:
            results = pool.map(_page_task, work, chunksize=1)
    else:
        results = [_page_task(w) for w in work]
    done = dict(zip(todo, results))
    pieces = [done.get(i, (T_ZERO, b'')) for i in range(npages)]
    pos = HEADER_SIZE + 4 * (npages + 1)                      # data starts after the table + end marker
    table = bytearray()
    for t, b in pieces:
        if pos > MAX_OFFSET:
            raise ValueError('output is too large for 29-bit offsets')
        table += struct.pack('<I', (t << TYPE_SHIFT) | pos)
        pos += len(b)
    if pos > MAX_OFFSET:
        raise ValueError('output is too large for 29-bit offsets')
    table += struct.pack('<I', (T_END << TYPE_SHIFT) | pos)   # end marker: where the stored data ends
    blob = SIGNATURE + struct.pack('<H', npages) + b'\x00\x00' + bytes(table) + b''.join(b for _, b in pieces)
    return blob, [(t, len(b)) for t, b in pieces]


def unpack(blob):
    """Returns the zero padded original file."""
    if len(blob) < HEADER_SIZE or blob[:4] != SIGNATURE:
        raise ValueError("not a CPZ file (needs the signature 'CPZ' followed by a 0 byte)")
    npages = struct.unpack_from('<H', blob, 4)[0]
    table_end = HEADER_SIZE + 4 * (npages + 1)                # the pages' entries + the end marker
    if table_end > len(blob):
        raise ValueError('page table runs past the end of the file')
    entries = [struct.unpack_from('<I', blob, HEADER_SIZE + 4 * i)[0] for i in range(npages + 1)]
    if entries[npages] >> TYPE_SHIFT != T_END:
        raise ValueError('the last table entry must be the end marker (type 110)')
    data_end = entries[npages] & MAX_OFFSET                   # from here on the file size is never used
    if data_end > len(blob):
        raise ValueError('end marker points past the end of the file (truncated?)')
    view = memoryview(blob)
    out = bytearray()
    for i in range(npages):
        t, off = entries[i] >> TYPE_SHIFT, entries[i] & MAX_OFFSET
        end = entries[i + 1] & MAX_OFFSET
        if off < table_end or end < off or end > data_end:
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
        elif t in (T_COMP, T_DICT):
            page, used = (decode if t == T_COMP else dict_decode)(view[:end], off, PAGE)   # cannot read past this page's data
            if used != n:
                raise ValueError(f'page {i}: compressed stream is {used} bytes but the table says {n}')
            out += page
        else:
            raise ValueError(f'page {i}: unknown page type {t:03b}')
    return bytes(out)


TYPE_NAMES = {T_RAW: 'uncompressed', T_DICT: 'dictionary', T_ZERO: 'zero', T_COMP: 'RLE'}


def report(src_len, out, info):
    print(f'{"page":>4}  {"type":<12} {"offset":>7} {"stored":>7}')
    pos = HEADER_SIZE + 4 * (len(info) + 1)
    for i, (t, n) in enumerate(info):
        print(f'{i:>4}  {TYPE_NAMES[t]:<12} {pos:>7} {n:>7}')
        pos += n
    print(f'{"end":>4}  {"end marker":<12} {pos:>7}')
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
    assert blob == b'CPZ\x00' + b'\x00\x00\x00\x00' + b'\x0c\x00\x00\xc0' and info == [] and unpack(blob) == b''   # just the end marker: type 110, offset 12
    blob, info = pack(bytes(PAGE))                                   # zero page: type 010 @16, end marker type 110 @16
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x10\x00\x00\x40' + b'\x10\x00\x00\xc0', blob.hex()
    blob, info = pack(b'\xAA' * PAGE)                                # type 2 @16, data FF BF AA, end marker @19
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x10\x00\x00\x80' + b'\x13\x00\x00\xc0' + b'\xff\xbf\xaa', blob.hex()
    blob, info = pack(b'\x01')                  # 1 byte -> padded; stream 80 01 FF 7E: "1 x 01" then 4095 zeros
    # (a 1-byte repeat and a 1-byte literal both cost 2 bytes; runs win ties, so 80 01 rather than 00 01)
    assert blob == b'CPZ\x00\x01\x00\x00\x00' + b'\x10\x00\x00\x80' + b'\x14\x00\x00\xc0' + b'\x80\x01\xff\x7e', blob.hex()
    assert unpack(blob) == b'\x01' + bytes(PAGE - 1)
    noise = bytes(rng.randint(1, 255) for _ in range(PAGE))         # nonzero, effectively incompressible
    assert len(encode(noise)) >= RAW_LIMIT
    blob, info = pack(bytes(PAGE) + noise + bytes(PAGE))             # zero, raw, zero: offsets 24, 24, 4120, end 4120
    assert struct.unpack_from('<IIII', blob, 8) == ((T_ZERO << TYPE_SHIFT) | 24, 24, (T_ZERO << TYPE_SHIFT) | (24 + PAGE), (T_END << TYPE_SHIFT) | (24 + PAGE)), struct.unpack_from('<IIII', blob, 8)
    assert len(blob) == 24 + PAGE and blob[24:] == noise and info == [(T_ZERO, 0), (T_RAW, PAGE), (T_ZERO, 0)]
    assert unpack(blob) == bytes(PAGE) + noise + bytes(PAGE)
    print('ok: container bytes match the hand-worked layouts (empty, zero page, tiny page, zero/raw/zero offsets, end marker)')

    # --- the 4090 rule: a page that compresses to 4089 bytes is compressed, 4090 is stored raw
    def page_with_literal(L):
        lit = bytes(1 + (i % 2) + 2 * (i % 7) for i in range(L))   # no two neighbours equal, never zero
        assert all(lit[i] != lit[i + 1] for i in range(L - 1))
        return lit + bytes(PAGE - L)
    for L, expect_len, expect_type in ((4086, 4089, T_COMP), (4087, 4090, T_RAW)):
        p = page_with_literal(L)
        assert len(encode(p)) == expect_len, (L, len(encode(p)))
        blob, info = pack(p, use_dict=False)                          # (dictionary mode would shrink this repeating pattern)
        assert info == [(expect_type, expect_len if expect_type == T_COMP else PAGE)], info
        assert unpack(blob) == p
    print('ok: RLE: a 4089-byte result is kept compressed, a 4090-byte result is stored uncompressed')

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
        'page entry with the end marker type': with_entry(good, 0, (T_END << TYPE_SHIFT) | (entry(good, 0) & MAX_OFFSET)),
        'unused type 011': with_entry(good, 0, (0b011 << TYPE_SHIFT) | (entry(good, 0) & MAX_OFFSET)),
        'unused type 101': with_entry(good, 0, (0b101 << TYPE_SHIFT) | (entry(good, 0) & MAX_OFFSET)),
        'unused type 111': with_entry(good, 0, (0b111 << TYPE_SHIFT) | (entry(good, 0) & MAX_OFFSET)),
        'zero page that has data': with_entry(good, 1, entry(good, 1) + 1),          # page 0 now spans 1 byte
        'compressed stream shorter than the table says': with_entry(good, 2, entry(good, 2) + 1),
        'compressed stream longer than the table says': with_entry(good, 2, entry(good, 2) - 1),
        'offset points into the table': with_entry(good, 2, entry(good, 2) & ~MAX_OFFSET | 4),
        'offsets out of order': with_entry(good, 1, (T_COMP << TYPE_SHIFT) | 9999999),
        'file cut short (end marker past the end)': good[:-1],
        'raw page one byte short': with_entry(good, 3, entry(good, 3) - 1),
        'end marker is not type 110': with_entry(good, 3, entry(good, 3) & MAX_OFFSET),
        'end marker before the last page': with_entry(good, 3, (T_END << TYPE_SHIFT) | 10),
        'compressed page marked raw': with_entry(good, 1, entry(good, 1) & MAX_OFFSET),
    }
    for name, b in bad_cases.items():
        try:
            unpack(b)
        except ValueError:
            continue
        raise SystemExit(f'accepted a corrupt file: {name}')
    assert unpack(good + b'\xde\xad\xbe\xef') == unpack(good)        # the end marker, not the file size, ends the data
    print(f'ok: {len(bad_cases)} kinds of corrupt file rejected; bytes after the end marker are ignored')
    try:
        _page_count(PAGE * (MAX_PAGES + 1))
        raise SystemExit('accepted too many pages')
    except ValueError:
        pass
    assert _page_count(PAGE * MAX_PAGES) == MAX_PAGES
    print('ok: 65535-page limit enforced')
    selftest_dictionary()


def selftest_dictionary():
    import random
    rng = random.Random(2024)

    def must_fail(buf, expected, why):
        try:
            dict_decode(buf, 0, expected)
        except ValueError:
            return
        raise SystemExit(f'dictionary decoder accepted: {why}')

    # --- decoder: streams worked out by hand
    ab = b'\x41\x42'
    cases = [
        (b'\x00' + ab + b'\x80\x80', 6, b'ABABAB'),                       # add AB (output it), then entry 0 twice
        (b'\x11' + ab, 2, b'AB'),                                          # literal that is not added: N = 1 means 2 bytes
        (b'\x10A', 1, b'A'),                                               # ... and N = 0 means ONE byte
        (b'\x23', 5, bytes(5)),                                            # 5 zeros: N = 3
        (b'\x31\x20', 18, bytes(18)),                                      # 18 zeros the long way: N = 16 = prefix 1, op 0
        (b'\x50', 18, bytes(18)),                                          # ... and in one byte
        (b'\x7F', 65, bytes(65)),                                          # the longest one byte zero run
        (b'\x40\x2A', 2, b'**'),                                           # repeat '*' (0x2A) N + 2 = 2 times
        (b'\x4F\x58', 17, b'X' * 17),
        (b'\x31\x40\x58', 18, b'X' * 18),                                  # repeat with a prefix: N = 16
        (b'\x4E\x00', 16, bytes(16)),                                      # repeating a zero byte is odd but legal
        (b'\x31\x30\x20', 258, bytes(258)),                                # N = 256 = prefixes 1, 0, op 0
        (b'\x31\x10' + bytes(range(1, 18)), 17, bytes(range(1, 18))),      # 17 byte literal: N = 16 = prefix 1, op 0
        (b'\x31\x11' + bytes(range(1, 19)), 18, bytes(range(1, 19))),      # 18 byte literal
        (b'\x0f' + bytes(range(65, 82)) + b'\x80\x23', 17 + 17 + 5, bytes(range(65, 82)) * 2 + bytes(5)),   # 17 byte entry
        (b'\x00AB\x00CD\x80\x81', 8, b'ABCDABCD'),                       # add AB, add CD, entry 0, entry 1
        (b'\x10A\x23\x10B', 7, b'A' + bytes(5) + b'B'),                   # one byte literals either side of a zero run
    ]
    for buf, n, want in cases:
        assert dict_decode(buf, 0, n) == (want, len(buf)), buf.hex()
    # the op that ends the page ends the stream: bytes after it are not consumed
    assert dict_decode(b'\x23\xFF\xFF', 0, 5) == (bytes(5), 1)

    # --- decoder: everything the format forbids is an error
    must_fail(b'\x11AB\x80', 4, 'entry used before it was added (a plain literal does not add)')
    must_fail(b'\x00AB\x81', 4, 'entry 1 when only entry 0 exists')
    must_fail(b'\x31\x00AB', 18, 'a prefix in front of an add-to-dictionary op')
    must_fail(b'\x00AB\x31\x80', 20, 'a prefix in front of an entry')
    must_fail(b'\x31\x50', 19, 'a prefix in front of a one byte zero run')
    must_fail(b'\x11A', 2, 'literal runs past the data')
    must_fail(b'\x11AB', 3, 'stream ends before the page does')
    must_fail(b'\x23', 4, 'zero run runs past the page')
    must_fail(b'\x50', 17, 'one byte zero run runs past the page')
    must_fail(b'\x00AB\x80\x80', 5, 'entry runs past the page')
    must_fail(b'\x42', 5, 'repeat with nothing to repeat')
    must_fail(b'\x42A', 5, 'repeat runs past the page')
    must_fail(b'\x31', 18, 'stream ends inside a prefix')
    must_fail(b'', 2, 'empty stream')
    must_fail(bytes([0x3F] * 200) + b'\x20', 4096, 'prefix chain that would overflow')
    full = b''.join(b'\x00' + bytes([i, 255 - i]) for i in range(DICT_MAX))                 # 128 distinct 2 byte entries
    want_full = b''.join(bytes([i, 255 - i]) for i in range(DICT_MAX))
    assert dict_decode(full + b'\x80\xFF', 0, 2 * DICT_MAX + 4)[0] == want_full + bytes([0, 255, 127, 128])
    must_fail(full + b'\x00AB', 2 * DICT_MAX + 2, 'a 129th dictionary entry')
    assert dict_decode(full + b'\x11AB', 0, 2 * DICT_MAX + 2)[1] == len(full) + 3              # ... but plain literals still fine
    print('ok: dictionary decoder: hand-worked streams, all 18 kinds of malformed stream rejected, 128 entry limit enforced')

    # --- the parse: compared with a brute force (every start, every length) on random small problems
    def brute(data, refs_in, forced_in, scale):
        n = len(data)
        spans = [(a, i) for i in range(n + 1) for a, l, e in forced_in[i]]
        dp = [DICT_INF] * (n + 1)
        dp[0] = 0
        for i in range(1, n + 1):
            for j in range(0, i):
                if dp[j] >= DICT_INF or any(a < i and j < b for a, b in spans):
                    continue
                hl = max(1, ((i - j - 1).bit_length() + 3) // 4)                       # literal: N = count - 1
                dp[i] = min(dp[i], dp[j] + (hl + i - j) * scale)
                c = i - j
                nib = max(1, ((c - 2).bit_length() + 3) // 4) if c >= 2 else None       # N = count - 2 in 4 bit groups
                if c >= 2 and not any(data[j:i]):                                      # zeros: 1 byte up to 65, else prefixes
                    dp[i] = min(dp[i], dp[j] + (1 if c <= 65 else nib) * scale)
                if c >= 2 and data[j] != 0 and data[j:i] == data[j:j + 1] * c:         # repeat of a non-zero byte, plus the byte
                    dp[i] = min(dp[i], dp[j] + (nib + 1) * scale)
            for j, l, e, c in refs_in[i]:
                dp[i] = min(dp[i], dp[j] + c)
            for j, l, e in forced_in[i]:
                dp[i] = min(dp[i], dp[j] + (1 + l) * scale)
        return dp[n]

    cases_run = 0
    for trial in range(300):
        n = rng.choice([2, 3, 4, 17, 18, 19, 40, 64, 65, 66, 67, 100, 257, 258, 259, 300, rng.randint(2, 320)])
        kind = rng.randint(0, 4)
        if kind == 4:                                          # runs of one byte, short and long
            data = b''.join(bytes([rng.choice([0, 0, 1, 255])]) * rng.choice([1, 2, 3, 17, 18, 64, 65, 66, 100, 260]) for _ in range(40))[:n]
            data += bytes(n - len(data))
        elif kind == 0:
            data = bytes(rng.randint(0, 3) for _ in range(n))
        elif kind == 1:
            data = bytes(n)
        elif kind == 2:
            data = bytes(rng.randint(0, 255) for _ in range(n))
        else:
            data = b''.join((bytes(rng.randint(1, 80)), bytes(rng.randint(1, 255) for _ in range(rng.randint(1, 20))))[rng.randint(0, 1)] for _ in range(n))[:n]
            data += bytes(n - len(data))
        scale = rng.choice([1, 8])
        forced_in = [[] for _ in range(n + 1)]
        taken = []
        for _ in range(rng.randint(0, 6)):
            L = rng.randint(2, 17)
            a = rng.randint(0, max(0, n - L))
            if a + L <= n and all(a + L <= x or y <= a for x, y in taken):
                taken.append((a, a + L))
                forced_in[a + L].append((a, L, ('F', a)))
        lb, cur, ends = [0] * (n + 1), 0, {a: b for a, b in taken}
        for i in range(n + 1):
            if i - 1 in ends:
                cur = max(cur, ends[i - 1])
            lb[i] = cur
        refs_in = [[] for _ in range(n + 1)]
        ref_costs = {}
        for _ in range(rng.randint(0, 40)):
            L = rng.randint(2, 17)
            a = rng.randint(0, max(0, n - L))
            if a + L <= n and a >= lb[a + L]:                  # the rule the encoder applies: not overlapping a forced entry
                refs_in[a + L].append((a, L, ('R', len(ref_costs)), rng.randint(scale, 3 * scale)))
                ref_costs[('R', len(ref_costs))] = refs_in[a + L][-1][3]
        segs, cost = _parse(data, refs_in, forced_in, lb, scale)
        want = brute(data, refs_in, forced_in, scale)
        assert cost == want, (trial, n, cost, want)
        # and the parse it returns is a real covering that costs what it says
        assert segs[0][1] == 0 and segs[-1][2] == n and all(segs[k][2] == segs[k + 1][1] for k in range(len(segs) - 1))
        assert all(b - a >= (1 if kind == 'L' else 2) for kind, a, b, _ in segs)
        assert {(a, b) for kind, a, b, _ in segs if kind == 'D'} == set(taken)
        for kind, a, b, e in segs:
            if kind == 'Z':
                assert not any(data[a:b])
            if kind == 'P':
                assert data[a] != 0 and data[a:b] == data[a:a + 1] * (b - a)
        total = 0
        for kind, a, b, e in segs:
            L = b - a
            if kind == 'L':
                total += (L + max(1, ((L - 1).bit_length() + 3) // 4)) * scale
            elif kind == 'Z':
                total += (1 if L <= 65 else max(1, ((L - 2).bit_length() + 3) // 4)) * scale
            elif kind == 'P':
                total += (max(1, ((L - 2).bit_length() + 3) // 4) + 1) * scale
            elif kind == 'D':
                total += (1 + L) * scale
            else:
                total += ref_costs[e]
        assert total == cost, (trial, total, cost)
        cases_run += 1
    print(f'ok: dictionary parse is exactly the cheapest covering: {cases_run} random problems match a brute force '
          f'(forced definitions, references, zero runs, repeats, header sizes up to 3 bytes)')

    # --- the encoder: always decodes back, never breaks the format's limits
    def motif_page(n, motifs, noise):
        ms = [bytes(rng.randint(0, 255) for _ in range(rng.randint(2, 17))) for _ in range(motifs)]
        out = bytearray()
        while len(out) < n:
            r = rng.random()
            out += rng.choice(ms) if r < 1 - noise else bytes(rng.randint(0, 255) for _ in range(rng.randint(1, 5))) if r < 1 - noise / 2 else bytes(rng.randint(1, 40))
        return bytes(out[:n])
    sizes_seen = 0
    for n in (0, 1, 2, 3, 4, 17, 18, 19, 255, 256, 257, 258, 259, 1000, PAGE):
        for page in ([bytes(n)] if n else [b'']) + [bytes(rng.randint(0, 255) for _ in range(n)), motif_page(n, 12, .1)]:
            e = dict_encode(page)
            assert dict_decode(e, 0, n) == (page, len(e))
            sizes_seen += 1
    for _ in range(12):
        page = motif_page(PAGE, rng.choice([5, 30, 100, 400]), rng.choice([0, .05, .3]))
        e = dict_encode(page)
        assert dict_decode(e, 0, PAGE) == (page, len(e))
        sizes_seen += 1
    many = b''.join(bytes([i, i ^ 0x55, 7]) for i in range(256))                          # 256 different 3 byte strings: more candidates than slots
    page = (many * 6)[:PAGE]                                                               # 768 * 6 >= 4096
    e = dict_encode(page)
    assert dict_decode(e, 0, PAGE) == (page, len(e)) and len(e) < len(encode(page)), (len(e), len(encode(page)))
    print(f'ok: dictionary encoder: {sizes_seen + 1} pages (empty, 1-4 bytes, header-size boundaries, noise, motifs) all decode back exactly')

    # --- effort levels: every level decodes, a higher level is never bigger, results don't vary between runs or with jobs
    gained = 0
    for size, motifs, noise_ in ((1200, 15, .1), (1500, 30, .2), (900, 8, 0), (1500, 60, .3)):
        page = motif_page(size, motifs, noise_)
        sizes = []
        for effort in (0, 1, 2):
            e = dict_encode(page, effort=effort)
            assert dict_decode(e, 0, size) == (page, len(e))
            sizes.append(len(e))
        assert sizes[0] >= sizes[1] >= sizes[2], sizes
        assert dict_encode(page, effort=2) == dict_encode(page, effort=2)
        gained += sizes[0] - sizes[2]
    pg = [motif_page(PAGE, 20, .1) for _ in range(3)]
    data3 = b''.join(pg)
    assert pack(data3, effort=0, jobs=2) == pack(data3, effort=0, jobs=1)
    assert pack(data3, effort=1, jobs=2)[0] == pack(data3, effort=1, jobs=1)[0]
    assert unpack(pack(data3, effort=1, jobs=2)[0]) == data3
    print(f'ok: effort 0/1/2 all decode, never get bigger with more effort (saved {gained} bytes on the test pages), deterministic, same output for any -j')

    # --- exact streams for a few small pages (the cheapest encoding is unique here)
    assert dict_encode(b'\xFF' * 100) == b'\x36\x42\xFF'                         # repeat: N = 98 = 0x62 -> prefix 6, op 2
    assert dict_encode(bytes(40)) == b'\x66'                                      # 40 zeros: one byte op, 0x50 + (40 - 18)
    assert dict_encode(bytes(65)) == b'\x7F' and dict_encode(bytes(66)) == b'\x34\x20'   # 66 zeros: N = 64 = 0x40 -> prefix 4, op 0
    assert dict_encode(b'A' + bytes(30) + b'B') == b'\x10A\x5C\x10B'              # literal, 30 zeros (0x50 + 12), literal
    assert dict_encode(b'AB' * 8) == b'\x06ABABABAB\x80'                         # add an 8 byte entry, then use it
    runs = 0
    for _ in range(40):                                                           # pages made of runs, noise and motifs
        page = b''.join(bytes([rng.choice([0, 0, 0xFF, 0x55, rng.randint(1, 255)])]) * rng.choice([1, 2, 5, 17, 18, 30, 65, 66, 200])
                        + (bytes(rng.randint(0, 255) for _ in range(rng.randint(0, 6))) if rng.random() < .5 else b'') for _ in range(300))[:PAGE]
        page += bytes(PAGE - len(page))
        e = dict_encode(page)
        assert dict_decode(e, 0, PAGE) == (page, len(e)) and len(e) <= len(encode(page)) + 40, (len(e), len(encode(page)))
        runs += 1
    print(f'ok: exact streams for hand-picked pages; {runs} run-heavy pages round trip (and stay close to RLE or beat it)')

    # --- choosing between the modes
    blocks = [bytes(rng.randint(1, 255) for _ in range(8)) for _ in range(40)]
    dict_page = b''.join(rng.choice(blocks) for _ in range(PAGE // 8))                    # no runs for RLE, lots of repeats
    assert len(encode(dict_page)) >= RAW_LIMIT
    blob, info = pack(dict_page)
    assert info[0][0] == T_DICT and info[0][1] < 1000, info
    assert struct.unpack_from('<II', blob, 8) == ((T_DICT << TYPE_SHIFT) | 16, (T_END << TYPE_SHIFT) | (16 + info[0][1]))
    assert unpack(blob) == dict_page
    assert pack(dict_page, use_dict=False)[1] == [(T_RAW, PAGE)]                          # without the mode it can only be stored
    ab_page = (b'AB' * (PAGE // 2))
    blob, info = pack(ab_page)
    assert info[0][0] == T_DICT and info[0][1] <= 300 and unpack(blob) == ab_page, info
    assert pack(b'\xAA' * PAGE)[1] == [(T_COMP, 3)]                                       # RLE is far smaller here
    noise = bytes(rng.randint(1, 255) for _ in range(PAGE))
    assert pack(noise)[1] == [(T_RAW, PAGE)]
    saved = globals()['encode'], globals()['dict_encode']
    try:
        probe = (bytes(range(1, 255)) * 17)[:PAGE]                                   # any non-zero page; the encoders are faked below
        for rle_n, dict_n, want in ((100, 100, T_COMP), (100, 99, T_DICT), (99, 100, T_COMP), (4089, 4090, T_COMP), (4090, 4089, T_DICT),
                                    (4090, 4090, T_RAW), (4089, 4089, T_COMP), (5000, 4089, T_DICT), (4089, 5000, T_COMP), (5000, 5000, T_RAW)):
            globals()['encode'] = lambda page, n=rle_n: bytes(n)
            globals()['dict_encode'] = lambda page, n=dict_n, **kw: bytes(n)
            t, b = choose_page_mode(probe)
            assert t == want, (rle_n, dict_n, t, want)
            assert len(b) == (PAGE if want == T_RAW else (rle_n if want == T_COMP else dict_n))
    finally:
        globals()['encode'], globals()['dict_encode'] = saved
    print('ok: each page takes the smaller of RLE and dictionary (RLE on a tie), only if under 4090 bytes; otherwise stored')

    # --- containers with dictionary pages: corrupt ones are rejected
    good, ginfo = pack(bytes(PAGE) + dict_page + ab_page)
    assert [t for t, _ in ginfo] == [T_ZERO, T_DICT, T_DICT]
    assert unpack(good) == bytes(PAGE) + dict_page + ab_page

    def with_entry(blob, k, value):
        b = bytearray(blob)
        struct.pack_into('<I', b, HEADER_SIZE + 4 * k, value)
        return bytes(b)
    e1 = struct.unpack_from('<I', good, HEADER_SIZE + 4)[0] & MAX_OFFSET
    bad = {
        'dictionary page marked RLE': with_entry(good, 1, (T_COMP << TYPE_SHIFT) | e1),
        'RLE page marked dictionary': with_entry(pack(b'\xAA' * PAGE)[0], 0, (T_DICT << TYPE_SHIFT) | 16),
        'dictionary stream shorter than the table says': with_entry(good, 2, struct.unpack_from('<I', good, HEADER_SIZE + 8)[0] + 1),
        'dictionary stream longer than the table says': with_entry(good, 2, struct.unpack_from('<I', good, HEADER_SIZE + 8)[0] - 1),
        'dictionary page marked raw': with_entry(good, 1, e1),
    }
    for name, b in bad.items():
        try:
            unpack(b)
        except ValueError:
            continue
        raise SystemExit(f'accepted a corrupt file: {name}')
    print(f'ok: {len(bad)} kinds of corrupt dictionary-page file rejected')

    # --- the larger limits
    assert MAX_OFFSET == (1 << 29) - 1 and (T_END << TYPE_SHIFT) == 0xC0000000 and (T_DICT << TYPE_SHIFT) == 0x20000000
    print('ok: 3 bit types, 29 bit offsets')


if __name__ == '__main__':
    args = sys.argv[1:]
    use_dict = '--no-dict' not in args
    args = [a for a in args if a != '--no-dict']
    effort, jobs = 1, 1
    for flag in ('--effort', '-j'):
        if flag in args:
            k = args.index(flag)
            try:
                val = int(args[k + 1])
            except (IndexError, ValueError):
                sys.exit(f'error: {flag} needs a number')
            effort, jobs = (val, jobs) if flag == '--effort' else (effort, val)
            del args[k:k + 2]
    if effort not in (0, 1, 2) or jobs < 1:
        sys.exit('error: --effort is 0, 1 or 2 and -j is at least 1')
    if args == ['selftest']:
        selftest()
    elif len(args) == 3 and args[0] == 'compress':
        src = open(args[1], 'rb').read()
        try:
            out, info = pack(src, use_dict=use_dict, effort=effort, jobs=jobs)
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