#!/usr/bin/env python3
"""Turn mermaid's click tooltips into real SVG tooltips.

Mermaid emits a click directive's tooltip as a `title="..."` ATTRIBUTE on the node's <a>. In HTML
that would show on hover; in SVG it does nothing at all - SVG takes its tooltip from a <title> CHILD
element. So the tooltips mermaid appears to support are silently absent from every standalone SVG.

This rewrites each `<a ... title="X">` to carry `<title>X</title>` as its first child, which is the
native SVG hover tooltip and works everywhere the picture does: GitHub, the docs site, Doxygen, and
the file opened on its own.

    python ci_tooling/assets/svg_tooltips.py docs/diagrams/foo.svg [more.svg ...]
"""
import html
import pathlib
import re
import sys

# The tooltip rides on the node's <g class="node ... clickable">, not on the <a> that wraps it, so
# both are matched rather than guessing which mermaid used this release.
ANCHOR = re.compile(r"<(a|g)\b([^>]*?)\btitle=\"([^\"]*)\"([^>]*)>")


def add_titles(svg):
    added = 0

    def repl(m):
        nonlocal added
        tag, before, tip, after = m.group(1), m.group(2), m.group(3), m.group(4)
        # The attribute stays: harmless, and it is what a browser's own a11y tree reads.
        added += 1
        return f'<{tag}{before}title="{tip}"{after}><title>{tip}</title>'

    return ANCHOR.sub(repl, svg), added


def main(paths):
    for p in paths:
        f = pathlib.Path(p)
        svg = f.read_text(encoding="utf-8")
        if "<title>" in svg:
            print(f"{f.name}: already has <title> children, skipped")
            continue
        out, n = add_titles(svg)
        f.write_text(out, encoding="utf-8", newline="\n")
        print(f"{f.name}: {n} tooltip(s)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1:])
