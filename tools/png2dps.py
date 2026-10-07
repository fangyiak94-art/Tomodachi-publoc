#!/usr/bin/env python3
"""Convert PNG art into the engine's DPS1 4-bit indexed image format.

Sprite sheet (frames side by side, transparent pixels -> index 0):
    tools/png2dps.py sheet.png out.dps --frame-width 24
Scene background (opaque, 16 colours, usually 240x240 or 120x120 at 2x):
    tools/png2dps.py house.png house.dps --background

Format (little endian):
    "DPS1" u16 width, u16 height, u16 frames, u16 reserved,
    16 x u16 RGB565 palette, then frames * ceil(w*h/2) bytes,
    two pixels per byte, high nibble first.
"""
import argparse
import struct
import sys

from PIL import Image


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(img, frame_w, background):
    img = img.convert("RGBA")
    w, h = img.size
    if frame_w is None:
        frame_w = w
    if w % frame_w:
        sys.exit(f"image width {w} is not a multiple of frame width {frame_w}")
    frames = w // frame_w
    if frame_w > 240 or h > 240 or frames > 64:
        sys.exit("too large: frames must be <= 240x240 and <= 64 frames")

    colours = 16 if background else 15  # index 0 is transparent for sprites
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img, mask=img.split()[3])
    q = rgb.quantize(colors=colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[: colours * 3]
    pal += [0] * (colours * 3 - len(pal))
    alpha = img.split()[3]

    palette = [0] * 16
    offset = 0 if background else 1
    for i in range(colours):
        palette[i + offset] = rgb565(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2])

    out = bytearray(b"DPS1")
    out += struct.pack("<HHHH", frame_w, h, frames, 0)
    out += struct.pack("<16H", *palette)
    qpx = q.load()
    apx = alpha.load()
    for f in range(frames):
        nibbles = []
        for y in range(h):
            for x in range(frame_w):
                sx = f * frame_w + x
                if not background and apx[sx, y] < 128:
                    nibbles.append(0)
                else:
                    nibbles.append(qpx[sx, y] + offset)
        if len(nibbles) % 2:
            nibbles.append(0)
        for i in range(0, len(nibbles), 2):
            out.append((nibbles[i] << 4) | nibbles[i + 1])
    return bytes(out), frames


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--frame-width", type=int)
    ap.add_argument("--background", action="store_true")
    a = ap.parse_args()
    data, frames = convert(Image.open(a.input), a.frame_width, a.background)
    with open(a.output, "wb") as f:
        f.write(data)
    print(f"{a.output}: {frames} frame(s), {len(data)} bytes")


if __name__ == "__main__":
    main()
