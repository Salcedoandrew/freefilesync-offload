#!/usr/bin/env python3
"""Rebuild AppIcon.icns from Assets.xcassets/AppIcon.appiconset/.

The app is built with swiftc and a hand-assembled bundle, not Xcode, so the
asset catalog is only the source of truth — the .icns is what the bundle
actually carries, and it is committed to the repo. Run this after replacing
the icon set, then rebuild the app:

    python3 make-icns.py
    make bundle

The file is the standard icns container: 'icns' + big-endian total length,
then one element per size (4-byte type + big-endian length + PNG data), the
same set png2icns writes. macOS accepts PNG data in every element.
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SET = os.path.join(HERE, "Assets.xcassets", "AppIcon.appiconset")

# type -> pixel size
ELEMENTS = [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128),
            ("ic08", 256), ("ic09", 512), ("ic10", 1024)]


def main() -> int:
    body = b""
    for etype, size in ELEMENTS:
        path = os.path.join(SET, f"{size}.png")
        with open(path, "rb") as f:
            data = f.read()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG"
        body += etype.encode("ascii") + struct.pack(">I", 8 + len(data)) + data

    out = os.path.join(HERE, "AppIcon.icns")
    with open(out, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)
    print(f"wrote {out}: {8 + len(body)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
