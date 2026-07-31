#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the favicon library: SVG favicons from a motif x palette matrix.

A favicon is a **motif** (a small recognizable glyph - bolt, chip, terminal, squid, ...) drawn in a
foreground color on a shaped background (rounded square / circle), so the set can grow to hundreds
without hand-drawing each. Each favicon is written as a crisp SVG under web_assets/favicons/; a separate
step (ci_tooling/assets/pack_favicons.sh) rasterizes each to the standard sizes (16/32/48/180/192/512 + .ico) and
packs a ready-to-drop-in tarball, plus a preview PNG for the gallery.

    python web_assets/wizard/gen_favicons.py            # write web_assets/favicons/*.svg
    python web_assets/wizard/gen_favicons.py --check     # CI: fail if stale
    python web_assets/wizard/gen_favicons.py --list       # print every favicon name
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "favicons"))

# Motifs: SVG inner markup drawn in a 64x64 viewBox, using {fg} for the glyph color. Kept simple so
# they stay legible at 16px. Recognizable + fun; `squid` nods to Squirty, the library mascot.
MOTIFS = {
    "bolt": '<path d="M36 6 18 36h12l-4 22 22-32H36l6-20z" fill="{fg}"/>',
    "terminal": '<path d="M14 22l10 10-10 10" stroke="{fg}" stroke-width="5" fill="none" stroke-linecap="round" stroke-linejoin="round"/><rect x="30" y="40" width="20" height="5" rx="2.5" fill="{fg}"/>',
    "chip": '<rect x="20" y="20" width="24" height="24" rx="4" fill="{fg}"/><g fill="{fg}"><rect x="27" y="10" width="4" height="8"/><rect x="33" y="10" width="4" height="8"/><rect x="27" y="46" width="4" height="8"/><rect x="33" y="46" width="4" height="8"/><rect x="10" y="27" width="8" height="4"/><rect x="10" y="33" width="8" height="4"/><rect x="46" y="27" width="8" height="4"/><rect x="46" y="33" width="8" height="4"/></g>',
    "wifi": '<g fill="none" stroke="{fg}" stroke-width="5" stroke-linecap="round"><path d="M18 30a20 20 0 0 1 28 0"/><path d="M25 37a10 10 0 0 1 14 0"/></g><circle cx="32" cy="44" r="3.5" fill="{fg}"/>',
    "signal": '<g fill="{fg}"><rect x="16" y="38" width="7" height="10" rx="1.5"/><rect x="27" y="30" width="7" height="18" rx="1.5"/><rect x="38" y="22" width="7" height="26" rx="1.5"/><rect x="49" y="14" width="0" height="0"/></g>',
    "hexagon": '<path d="M32 12l17 10v20L32 52 15 42V22z" fill="none" stroke="{fg}" stroke-width="5" stroke-linejoin="round"/>',
    "cube": '<path d="M32 12l18 10v20L32 52 14 42V22z" fill="none" stroke="{fg}" stroke-width="4" stroke-linejoin="round"/><path d="M14 22l18 10 18-10M32 32v20" stroke="{fg}" stroke-width="4" fill="none"/>',
    "gear": '<g fill="{fg}"><circle cx="32" cy="32" r="8" fill="none" stroke="{fg}" stroke-width="5"/><g stroke="{fg}" stroke-width="6" stroke-linecap="round"><path d="M32 12v6M32 46v6M12 32h6M46 32h6M18 18l4 4M42 42l4 4M46 18l-4 4M22 42l-4 4"/></g></g>',
    "wave": '<path d="M10 38q6-14 12 0t12 0 12 0 8 0" fill="none" stroke="{fg}" stroke-width="5" stroke-linecap="round"/>',
    "shield": '<path d="M32 10l18 6v14c0 12-9 20-18 24-9-4-18-12-18-24V16z" fill="none" stroke="{fg}" stroke-width="5" stroke-linejoin="round"/>',
    "star": '<path d="M32 12l6 14 15 1-11 10 3 15-13-8-13 8 3-15-11-10 15-1z" fill="{fg}"/>',
    "heart": '<path d="M32 50S12 38 12 25a11 11 0 0 1 20-6 11 11 0 0 1 20 6c0 13-20 25-20 25z" fill="{fg}"/>',
    "leaf": '<path d="M16 48C16 24 40 16 50 14c2 22-8 36-34 34z" fill="{fg}"/><path d="M22 42q12-14 24-20" stroke="{fg}" stroke-width="3" fill="none"/>',
    "rocket": '<path d="M32 8c10 6 14 18 12 30l-6 6h-12l-6-6C18 26 22 14 32 8z" fill="{fg}"/><circle cx="32" cy="26" r="5" fill="none" stroke="#00000055" stroke-width="3"/><path d="M26 44l-6 10 8-4M38 44l6 10-8-4" fill="{fg}"/>',
    "squid": '<g fill="{fg}"><path d="M32 12c9 0 15 7 15 16v6H17v-6c0-9 6-16 15-16z"/><circle cx="26" cy="28" r="3" fill="#00000066"/><circle cx="38" cy="28" r="3" fill="#00000066"/><g stroke="{fg}" stroke-width="4" stroke-linecap="round"><path d="M20 36v12M27 36v14M34 36v14M41 36v12"/></g></g>',
    "dot-grid": '<g fill="{fg}"><circle cx="22" cy="22" r="4"/><circle cx="32" cy="22" r="4"/><circle cx="42" cy="22" r="4"/><circle cx="22" cy="32" r="4"/><circle cx="32" cy="32" r="4"/><circle cx="42" cy="32" r="4"/><circle cx="22" cy="42" r="4"/><circle cx="32" cy="42" r="4"/><circle cx="42" cy="42" r="4"/></g>',
    "globe": '<circle cx="32" cy="32" r="20" fill="none" stroke="{fg}" stroke-width="4"/><path d="M12 32h40M32 12c8 8 8 32 0 40M32 12c-8 8-8 32 0 40" fill="none" stroke="{fg}" stroke-width="3"/>',
    "pulse": '<path d="M10 32h10l4-12 8 24 6-18 4 6h12" fill="none" stroke="{fg}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>',
}

# Color pairs (name -> (background, foreground)). A curated spread: brand, neutrals, and vivids so a
# picky user finds a match; pairs with the theme accents. Backgrounds are solid for crisp 16px icons.
PAIRS = {
    "indigo": ("#4f46e5", "#ffffff"),
    "slate": ("#334155", "#e2e8f0"),
    "emerald": ("#059669", "#ecfdf5"),
    "amber": ("#d97706", "#fffbeb"),
    "rose": ("#e11d48", "#fff1f2"),
    "cyan": ("#0891b2", "#ecfeff"),
    "violet": ("#7c3aed", "#f5f3ff"),
    "charcoal": ("#1f2937", "#f9fafb"),
    "ink": ("#0d1117", "#58a6ff"),
    "paper": ("#f8fafc", "#0f172a"),
    "dracula": ("#282a36", "#bd93f9"),
    "nord": ("#2e3440", "#88c0d0"),
    "matrix": ("#000000", "#00ff41"),
    "hotdog": ("#ff0000", "#ffff00"),
    "sky": ("#0ea5e9", "#f0f9ff"),
    "sunset": ("#f97316", "#450a0a"),
}

# Background shapes.
SHAPES = {
    "rounded": '<rect x="2" y="2" width="60" height="60" rx="14" fill="{bg}"/>',
    "circle": '<circle cx="32" cy="32" r="30" fill="{bg}"/>',
    "square": '<rect x="2" y="2" width="60" height="60" rx="4" fill="{bg}"/>',
}

# The set: pick a shape per motif so the library has visual variety without exploding the count.
MOTIF_SHAPE = {m: ("circle" if i % 3 == 0 else "rounded" if i % 3 == 1 else "square") for i, m in enumerate(MOTIFS)}


def svg(motif, pair):
    bg, fg = PAIRS[pair]
    shape = SHAPES[MOTIF_SHAPE[motif]].format(bg=bg)
    glyph = MOTIFS[motif].format(fg=fg)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">' f"{shape}{glyph}</svg>\n"
    )


def catalog():
    """Every (name, motif, pair). name = <motif>-<pair>."""
    return [(f"{m}-{p}", m, p) for m in MOTIFS for p in PAIRS]


GALLERY = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "..", "docs", "FAVICONS.md"))


def cmd_gallery():
    """Write docs/FAVICONS.md: the SVGs shown inline, grouped by motif (SVG renders on GitHub)."""
    lines = [
        "# Favicons",
        "",
        "> Generated by `web_assets/wizard/gen_favicons.py`; do not edit by hand.",
        "",
        f"**{len(catalog())} favicons** - {len(MOTIFS)} motifs x {len(PAIRS)} palettes - as crisp SVG under"
        " [`web_assets/favicons/`](../src/web/favicons/). Package any one as a drop-in favicon set (16/32/48/180/"
        "192/512 PNG + `favicon.ico` + `site.webmanifest`, tarballed) with"
        " [`ci_tooling/assets/pack_favicons.sh`](../ci_tooling/assets/pack_favicons.sh) `<name>` (needs `rsvg-convert` + ImageMagick).",
        "",
    ]
    for motif in MOTIFS:
        lines.append(f"### {motif}")
        lines.append("")
        row = " ".join(
            f'<img src="../src/web/favicons/{motif}-{p}.svg" width="44" alt="{motif}-{p}" title="{motif}-{p}">'
            for p in PAIRS
        )
        lines.append(row)
        lines.append("")
    with open(GALLERY, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines).rstrip("\n") + "\n")
    print(f"wrote {os.path.relpath(GALLERY)}")
    return 0


def main():
    if sys.argv[1:2] == ["gallery"]:
        return cmd_gallery()
    check = "--check" in sys.argv[1:]
    if "--list" in sys.argv[1:]:
        for name, _, _ in catalog():
            print(name)
        return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    stale = []
    for name, motif, pair in catalog():
        path = os.path.join(OUT_DIR, name + ".svg")
        content = svg(motif, pair)
        old = open(path, encoding="utf-8").read() if os.path.isfile(path) else None
        if old != content:
            stale.append(name)
            if not check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(content)
    total = len(catalog())
    if check:
        if stale:
            sys.stderr.write("STALE favicons (run gen_favicons.py): " + ", ".join(stale[:8]) + "...\n")
            return 1
        print(f"{total} favicons in sync")
        return 0
    print(f"generated {total} favicons ({len(MOTIFS)} motifs x {len(PAIRS)} palettes) -> {os.path.relpath(OUT_DIR)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
