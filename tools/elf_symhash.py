#!/usr/bin/env python3
"""
Dump exported ELF symbols as a flat binary table.

For every exported symbol in the ELF's .symtab (names from the linked .strtab;
defined, GLOBAL/WEAK binding, default visibility, non-zero address, and
address >= 0xC0000000; override with --min-address).

Output layout (all integers little-endian regardless of the ELF's endianness),
symbols sorted by address:

    <symbol count: 2 bytes, unsigned>
    <base address: 4 bytes (ELF32) or 8 bytes (ELF64) = lowest symbol address>
    then per symbol:
        <8 bytes: FNV-1a 64 hash of name, top bit cleared>
        <2 bytes: unsigned delta from the previous symbol's address>

The first symbol's delta is 0 (it is the base address). Total file size is
2 + 4 + N*10 bytes for ELF32 (2 + 8 + N*10 for ELF64). If any delta does not
fit in 16 bits, or N does not fit in 16 bits, the script prints an error and
exits non-zero without writing output.

Usage:
    elf_symhash.py libfoo.so -o symbols.bin
    elf_symhash.py libfoo.so -o symbols.bin --list
"""
import argparse
import struct
import sys

# ELF constants
SHT_SYMTAB = 2
SHN_UNDEF = 0
SHN_LORESERVE = 0xFF00
SHN_ABS = 0xFFF1
STB_GLOBAL, STB_WEAK, STB_GNU_UNIQUE = 1, 2, 10
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_GNU_IFUNC = 0, 1, 2, 10
STV_DEFAULT = 0

MIN_ADDRESS = 0xC0000000    # symbols below this address are ignored

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK64 = 0xFFFFFFFFFFFFFFFF


def hash_symbol(name: bytes) -> int:
    h = FNV_OFFSET
    for b in name:
        h ^= b
        h = (h * FNV_PRIME) & MASK64
    return h & 0x7FFFFFFFFFFFFFFF


def is_visible_symbol(st_value: int, st_other: int) -> bool:
    """Mirror of the runtime IsVisibleSymbol() check."""
    if st_value == 0:
        return False
    # Skip "hidden" and "internal" (and protected) symbols.
    return (st_other & 3) == 0


def parse_exports(data: bytes):
    """Return list of (name_bytes, address) for exported .symtab symbols."""
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    is64 = data[4] == 2
    endian = "<" if data[5] == 1 else ">"

    if is64:
        (e_shoff,) = struct.unpack_from(endian + "Q", data, 0x28)
        e_shentsize, e_shnum = struct.unpack_from(endian + "HH", data, 0x3A)
        sh_fmt = endian + "IIQQQQIIQQ"
        sym_fmt = endian + "IBBHQQ"          # name, info, other, shndx, value, size
        unpack_sym = lambda b, o: (lambda n, i, ot, s, v, sz: (n, i, ot, s, v))(
            *struct.unpack_from(sym_fmt, b, o))
    else:
        (e_shoff,) = struct.unpack_from(endian + "I", data, 0x20)
        e_shentsize, e_shnum = struct.unpack_from(endian + "HH", data, 0x2E)
        sh_fmt = endian + "IIIIIIIIII"
        sym_fmt = endian + "IIIBBH"          # name, value, size, info, other, shndx
        unpack_sym = lambda b, o: (lambda n, v, sz, i, ot, s: (n, i, ot, s, v))(
            *struct.unpack_from(sym_fmt, b, o))

    if e_shoff == 0 or e_shnum == 0:
        raise ValueError("ELF has no section headers (stripped?); cannot locate .symtab")

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        if is64:
            (_, sh_type, _, _, sh_offset, sh_size, sh_link, _, _, sh_entsize) = \
                struct.unpack_from(sh_fmt, data, off)
        else:
            (_, sh_type, _, _, sh_offset, sh_size, sh_link, _, _, sh_entsize) = \
                struct.unpack_from(sh_fmt, data, off)
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    symtab = next((s for s in sections if s[0] == SHT_SYMTAB), None)
    if symtab is None:
        raise ValueError("no .symtab section found (binary stripped?)")
    _, sym_off, sym_size, str_idx, entsize = symtab
    entsize = entsize or (24 if is64 else 16)
    _, str_off, str_size, _, _ = sections[str_idx]
    strtab = data[str_off:str_off + str_size]

    out = []
    for i in range(1, sym_size // entsize):   # skip null symbol 0
        name_off, info, other, shndx, value = unpack_sym(data, sym_off + i * entsize)
        bind, typ = info >> 4, info & 0xF
        vis = other & 0x3

        if shndx == SHN_UNDEF:                       # undefined, not exported
            continue
        if bind not in (STB_GLOBAL, STB_WEAK, STB_GNU_UNIQUE):
            continue
        if not is_visible_symbol(value, other):      # zero address / non-default visibility
            continue
        if typ not in (STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_GNU_IFUNC):
            continue                                 # skips TLS, SECTION, FILE, ...

        end = strtab.index(b"\0", name_off)
        name = strtab[name_off:end]
        if not name:
            continue
        out.append((name, value))
    return is64, out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("-o", "--output", required=True, help="output .bin path")
    ap.add_argument("--list", action="store_true",
                    help="print name, address and hash for each symbol")
    ap.add_argument("--min-address", type=lambda s: int(s, 0), default=MIN_ADDRESS,
                    help="ignore symbols below this address (default: 0x%(default)X)")
    args = ap.parse_args()

    with open(args.elf, "rb") as f:
        data = f.read()

    try:
        is64, symbols = parse_exports(data)
    except ValueError as e:
        sys.exit(f"error: {e}")
    symbols = [(n, a) for n, a in symbols if a >= args.min_address]
    if not symbols:
        sys.exit(f"error: no exported visible symbols at or above 0x{args.min_address:X}")

    # records: (hash, address, name)
    records = [(hash_symbol(name), addr, name) for name, addr in symbols]

    # Sort by hash (internal use only) to flag collisions, which would make
    # hash lookups ambiguous.
    by_hash = sorted(records, key=lambda r: r[0])
    for (h1, _, n1), (h2, _, n2) in zip(by_hash, by_hash[1:]):
        if h1 == h2 and n1 != n2:
            print(f"warning: hash collision 0x{h1:016x}: {n1!r} vs {n2!r}",
                  file=sys.stderr)

    # Final output order: by address (hash breaks ties so output is deterministic).
    records.sort(key=lambda r: (r[1], r[0]))

    # Encode everything first so a failure leaves no partial output file.
    if len(records) > 0xFFFF:
        sys.exit(f"error: {len(records)} symbols does not fit in the 2-byte symbol count")
    base = records[0][1]
    out = bytearray(struct.pack("<H", len(records)))
    out += struct.pack("<Q" if is64 else "<I", base)
    prev = base
    for h, addr, name in records:
        delta = addr - prev
        if delta > 0xFFFF:
            sys.exit(f"error: cannot encode delta 0x{delta:x} to symbol "
                     f"{name.decode(errors='replace')!r} (address 0x{addr:x}) from "
                     f"previous address 0x{prev:x}: does not fit in 16 bits")
        out += struct.pack("<QH", h, delta)
        prev = addr

    if args.list:
        for h, addr, name in records:
            print(f"0x{addr:08x}  0x{h:016x}  {name.decode(errors='replace')}")

    with open(args.output, "wb") as f:
        f.write(out)

    print(f"wrote {len(records)} symbols ({len(out)} bytes, "
          f"2-byte count, {8 if is64 else 4}-byte base 0x{base:x}) to {args.output}",
          file=sys.stderr)


if __name__ == "__main__":
    main()