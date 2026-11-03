#!/usr/bin/env python3
"""Pack PNG files into a macOS .icns container (stdlib only).

Usage: make_icns.py OUT.icns TYPE=PNG [TYPE=PNG ...]

TYPE is a four-character icns element type such as ic07 or ic14; PNG is the
file whose bytes are stored as that element. Element layout:
  'icns' + big-endian total length (header included)
  repeated: 4-char type + big-endian length (8-byte header included) + data
"""

import struct
import sys

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def build_icns(entries):
    body = bytearray()
    for icon_type, png_path in entries:
        if len(icon_type) != 4:
            raise ValueError("icns type must be 4 characters: %r" % icon_type)
        with open(png_path, "rb") as handle:
            data = handle.read()
        if not data.startswith(PNG_SIGNATURE):
            raise ValueError("not a PNG file: %s" % png_path)
        body += icon_type.encode("ascii")
        body += struct.pack(">I", len(data) + 8)
        body += data
    return b"icns" + struct.pack(">I", len(body) + 8) + bytes(body)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    out_path = argv[1]
    entries = []
    for pair in argv[2:]:
        if "=" not in pair:
            sys.stderr.write("expected TYPE=PNG, got %r\n" % pair)
            return 2
        icon_type, png_path = pair.split("=", 1)
        entries.append((icon_type, png_path))
    with open(out_path, "wb") as handle:
        handle.write(build_icns(entries))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
