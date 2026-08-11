"""Render source files to numbered PNG pages, for surveying at image density.

    src2png.py <file> <out_stem> [lines_per_page] [pt] [start] [end]
    src2png.py <dir> <dest> [kb_per_page] [pt]

The directory form walks <dir>, renders every file whose extension is in
WALK_EXTS, and writes <dest>/<name>_<ext>_<n>.png. Pages break on whole lines
once the page holds kb_per_page kilobytes, so a line never splits across two.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\consola.ttf",
    r"C:\Windows\Fonts\cour.ttf",
    r"C:\Windows\Fonts\lucon.ttf",
]

WALK_EXTS = (".txt", ".py", ".c", ".h", ".cpp")


def load_font(size):
    for path in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def char_width(font):
    probe = Image.new("RGB", (10, 10))
    d = ImageDraw.Draw(probe)
    return d.textlength("M" * 100, font=font) / 100.0


def by_lines(lines, lines_per_page):
    """Chunks of lines_per_page lines, each with its offset into lines."""
    for p0 in range(0, len(lines), lines_per_page):
        yield p0, lines[p0 : p0 + lines_per_page]


def by_bytes(lines, kb_per_page):
    """Chunks holding at most kb_per_page kilobytes, split only between lines."""
    limit = kb_per_page * 1024
    p0 = 0
    used = 0
    chunk = []
    for i, text in enumerate(lines):
        if chunk and used + len(text) + 1 > limit:
            yield p0, chunk
            p0 = i
            used = 0
            chunk = []
        chunk.append(text)
        used += len(text) + 1
    if chunk:
        yield p0, chunk


def write_page(chunk, first_lineno, name, font, cw, size):
    """One page: the line number in grey, the line in black, clipped at 160 columns."""
    lh = size + 5
    widest = min(max((len(x) for x in chunk), default=1), 160)
    W = int(cw * (widest + 7)) + 24
    H = lh * len(chunk) + 20
    img = Image.new("RGB", (W, H), (255, 255, 255))
    dr = ImageDraw.Draw(img)
    for i, text in enumerate(chunk):
        dr.text((10, 10 + i * lh), "{0:>5} ".format(first_lineno + i), font=font, fill=(150, 150, 150))
        dr.text((10 + cw * 6, 10 + i * lh), text[:160], font=font, fill=(0, 0, 0))
    img.save(name)
    return W, H


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read().split("\n")


def render_file(src, out_stem, lines_per_page, size, start, end, font, cw):
    lines = read_lines(src)
    if end <= 0 or end > len(lines):
        end = len(lines)
    lines = lines[start - 1 : end]
    out = []
    for n, (p0, chunk) in enumerate(by_lines(lines, lines_per_page), 1):
        name = "{0}_{1}.png".format(out_stem, n)
        W, H = write_page(chunk, start + p0, name, font, cw, size)
        out.append((name, W, H))
    return out


def render_tree(root, dest, kb_per_page, size, font, cw):
    if not os.path.isdir(dest):
        os.makedirs(dest)
    out = []
    for dirpath, _, filenames in os.walk(root):
        for fn in sorted(filenames):
            stem, ext = os.path.splitext(fn)
            if ext.lower() not in WALK_EXTS:
                continue
            src = os.path.join(dirpath, fn)
            lines = read_lines(src)
            base = os.path.join(dest, "{0}_{1}".format(stem, ext.lstrip(".").lower()))
            for n, (p0, chunk) in enumerate(by_bytes(lines, kb_per_page), 1):
                name = "{0}_{1}.png".format(base, n)
                W, H = write_page(chunk, 1 + p0, name, font, cw, size)
                out.append((name, W, H))
    return out


def main():
    src = sys.argv[1]
    dst = sys.argv[2]

    if os.path.isdir(src):
        kb_per_page = int(sys.argv[3]) if len(sys.argv) > 3 else 8
        size = int(sys.argv[4]) if len(sys.argv) > 4 else 15
        font = load_font(size)
        pages = render_tree(src, dst, kb_per_page, size, font, char_width(font))
    else:
        lines_per_page = int(sys.argv[3]) if len(sys.argv) > 3 else 200
        size = int(sys.argv[4]) if len(sys.argv) > 4 else 15
        start = int(sys.argv[5]) if len(sys.argv) > 5 else 1
        end = int(sys.argv[6]) if len(sys.argv) > 6 else 0
        font = load_font(size)
        pages = render_file(src, dst, lines_per_page, size, start, end, font, char_width(font))

    for name, W, H in pages:
        print("{0}  {1}x{2}".format(name, W, H))


if __name__ == "__main__":
    main()
