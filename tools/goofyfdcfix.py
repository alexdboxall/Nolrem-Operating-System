#!/usr/bin/env python3

import os
import sys
SECTOR = 512
SECTORS_PER_TRACK = 18
TRACK = SECTOR * SECTORS_PER_TRACK  # 9216 bytes == 9 KiB

def head0ify(data: bytes, track: int = TRACK) -> bytes:
    """Keep the front half, then interleave one track of zeros after each track."""
    keep = data[: len(data) // 2]
    zeros = bytes(track)

    out = bytearray()
    for i in range(0, len(keep), track):
        chunk = keep[i : i + track]
        if len(chunk) < track:  # runt final track, pad it out
            chunk = chunk + bytes(track - len(chunk))
        out += chunk
        out += zeros
    return bytes(out)


path = sys.argv[1]

with open(path, "rb") as f:
    data = f.read()

result = head0ify(data)

with open(path, "r+b") as f:
    f.write(result)
    f.truncate(len(result))
    f.flush()
    os.fsync(f.fileno())