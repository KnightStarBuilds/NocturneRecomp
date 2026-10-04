#!/usr/bin/env python3
"""Encode the existing public-domain moon as a 512px PS4 PNG (stdlib only)."""
from pathlib import Path
import re
import struct
import zlib


def main():
    root = Path(__file__).resolve().parents[2]
    header = (root / "src/shims/core/window_icon_moon.h").read_text()
    rgba = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
    if len(rgba) != 128 * 128 * 4:
        raise ValueError("Unexpected moon artwork dimensions")
    rows = bytearray()
    for y in range(512):
        rows.append(0)
        for x in range(512):
            index = ((y // 4) * 128 + x // 4) * 4
            alpha = rgba[index + 3]
            rows.extend((rgba[index + channel] * alpha + 12 * (255 - alpha) + 127) // 255 for channel in range(3))
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", 512, 512, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    (Path(__file__).resolve().parent / "icon0.png").write_bytes(png)


if __name__ == "__main__":
    main()
