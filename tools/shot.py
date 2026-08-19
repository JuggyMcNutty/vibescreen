#!/usr/bin/env python3
"""Screenshot the simulator window as a PNG.

    python3 tools/shot.py out.png
    python3 tools/shot.py out.png --name "TFT Simulator" --settle 8

Why this exists rather than `xwd | xwdtopnm | pnmtopng`: on a 32 bpp TrueColor
visual xwdtopnm emits maxval 65535 and maps the channels wrongly, which turns
the whole UI a flat blue. It does not fail, it just quietly lies, so a colour
bug and a capture bug look identical.

Decoding the masks by hand is not enough on its own either, which is the trap
this tool exists to close. The XWD header is big-endian, but the pixel data is
in the server's byte order, named by the header's own byte_order field. Read
32 bpp pixels big-endian on a little-endian server and every channel shifts by
one byte: red and green swap, and the alpha byte lands where blue should be, so
the background reads (43, 40, 255) instead of (40, 43, 48). That is a plausible
looking image, which is what makes it dangerous. Measured 2026-08-19.

The settle loop is bounded. The main panel's temperature chart redraws every
frame, so "grab until two consecutive grabs are identical" never terminates
there. Take the last grab and move on instead of hanging.
"""

import argparse
import hashlib
import struct
import subprocess
import sys
import time
import zlib

# Field order of XWDFileHeader, all big-endian uint32.
(HDR_SIZE, _VERSION, _FORMAT, _DEPTH, WIDTH, HEIGHT, _XOFF, BYTE_ORDER,
 _BMP_UNIT, _BMP_BIT_ORDER, _BMP_PAD, BPP, BYTES_PER_LINE, _VIS_CLASS,
 RED_MASK, GREEN_MASK, BLUE_MASK, _BITS_PER_RGB, _CMAP_ENTRIES,
 NCOLORS) = range(20)


def grab(window_name):
    return subprocess.run(["xwd", "-silent", "-name", window_name],
                          capture_output=True, check=True).stdout


def settle(window_name, tries):
    """Grab until two in a row match, or until tries runs out."""
    prev = prev_digest = None
    for _ in range(max(1, tries)):
        cur = grab(window_name)
        digest = hashlib.md5(cur).digest()
        if prev_digest == digest:
            return cur
        prev, prev_digest = cur, digest
        time.sleep(0.4)
    return prev


def decode(blob):
    """XWD to (width, height, rows of packed RGB)."""
    h = struct.unpack(">20I", blob[:80])
    w, height, bpp, stride = h[WIDTH], h[HEIGHT], h[BPP], h[BYTES_PER_LINE]
    if bpp not in (24, 32):
        raise SystemExit("unsupported bits per pixel: %d" % bpp)

    # The header is big-endian; the pixels are in the server's byte order.
    order = "big" if h[BYTE_ORDER] else "little"
    masks = (h[RED_MASK], h[GREEN_MASK], h[BLUE_MASK])
    if not all(masks):
        raise SystemExit("no RGB masks in header, is this a TrueColor visual?")
    shifts = [(m & -m).bit_length() - 1 for m in masks]

    start = h[HDR_SIZE] + h[NCOLORS] * 12   # skip the colormap entries
    step = bpp // 8
    rows = []
    for y in range(height):
        base = start + y * stride
        row = bytearray(b"\x00")            # PNG filter byte: none
        for x in range(w):
            off = base + x * step
            px = int.from_bytes(blob[off:off + step], order)
            row += bytes(((px & m) >> s) & 0xFF for m, s in zip(masks, shifts))
        rows.append(bytes(row))
    return w, height, rows


def write_png(path, w, h, rows):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8-bit truecolour
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", help="PNG to write")
    ap.add_argument("--name", default="TFT Simulator", help="X window name")
    ap.add_argument("--settle", type=int, default=6,
                    help="how many grabs to try before giving up on a still "
                         "frame (default 6)")
    args = ap.parse_args()

    try:
        blob = settle(args.name, args.settle)
    except FileNotFoundError:
        raise SystemExit("xwd not found: install x11-apps")
    except subprocess.CalledProcessError:
        raise SystemExit("no window named %r. Is the simulator running, and "
                         "was it started with SDL_VIDEODRIVER=x11?" % args.name)

    w, h, rows = decode(blob)
    write_png(args.out, w, h, rows)

    # Print the top-left pixel. On the dark theme it should be near (40,43,48);
    # a blue channel near 255 means the byte order went wrong again.
    corner = rows[2][1:4]
    print("%s  %dx%d  corner rgb=(%d, %d, %d)"
          % (args.out, w, h, corner[0], corner[1], corner[2]))


if __name__ == "__main__":
    sys.exit(main())
