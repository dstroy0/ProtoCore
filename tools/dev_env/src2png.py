"""Render a source file to numbered PNG pages, for surveying a file at image density."""

import sys

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\consola.ttf",
    r"C:\Windows\Fonts\cour.ttf",
    r"C:\Windows\Fonts\lucon.ttf",
]


def load_font(size):
    for path in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def main():
    src = sys.argv[1]
    out_stem = sys.argv[2]
    lines_per_page = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    size = int(sys.argv[4]) if len(sys.argv) > 4 else 15
    start = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    end = int(sys.argv[6]) if len(sys.argv) > 6 else 0

    with open(src, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    if end <= 0 or end > len(lines):
        end = len(lines)
    lines = lines[start - 1 : end]

    font = load_font(size)
    probe = Image.new("RGB", (10, 10))
    d = ImageDraw.Draw(probe)
    cw = d.textlength("M" * 100, font=font) / 100.0
    lh = size + 5

    pages = []
    for p0 in range(0, len(lines), lines_per_page):
        chunk = lines[p0 : p0 + lines_per_page]
        widest = max((len(x) for x in chunk), default=1)
        widest = min(widest, 160)
        W = int(cw * (widest + 7)) + 24
        H = lh * len(chunk) + 20
        img = Image.new("RGB", (W, H), (255, 255, 255))
        dr = ImageDraw.Draw(img)
        for i, text in enumerate(chunk):
            n = start + p0 + i
            dr.text((10, 10 + i * lh), "{0:>5} ".format(n), font=font, fill=(150, 150, 150))
            dr.text((10 + cw * 6, 10 + i * lh), text[:160], font=font, fill=(0, 0, 0))
        name = "{0}_{1}.png".format(out_stem, len(pages) + 1)
        img.save(name)
        pages.append((name, W, H))

    for name, W, H in pages:
        print("{0}  {1}x{2}".format(name, W, H))


if __name__ == "__main__":
    main()
