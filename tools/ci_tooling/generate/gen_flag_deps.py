#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the build-flag dependency flowchart from the config header's own guards.

The single source of truth is src/protocore_config.h. A child feature that cannot compile
without a parent is enforced there by a preprocessor guard whose #error says "... requires ...":

    #if PC_ENABLE_CHILD && !PC_ENABLE_PARENT
    #error "... PC_ENABLE_CHILD requires PC_ENABLE_PARENT"
    #endif

and a PSRAM-class feature by a guard whose #error mentions PSRAM:

    #if PC_ENABLE_HTTP2 && defined(ARDUINO) && !PC_H2_POOL_IN_PSRAM
    #error "... PC_ENABLE_HTTP2 needs PSRAM ..."
    #endif

This script walks the header with a preprocessor-condition stack. For each #error it reads the
active conditions - the positive PC_ENABLE_* term is the feature being built (the child), each
negated PC_* term a prerequisite - AND the error message: a "requires" message yields a hard
feature edge, a "PSRAM" message a resource gate, anything else (a worker-stack floor, a size range)
is ignored. Gating on the message is what keeps a scoping clause like `!PC_ENABLE_SSH` in a
stack-size guard from being mistaken for "OIDC requires SSH". Auto-derived flags (a `#define
PC_ENABLE_X 1` guarded by an OR of other ENABLE flags) are read too. The graph is injected into
the "Build-flag dependencies" section of README.md between generated markers, so it can never drift
from the guards the compiler actually enforces.

Run from the repo root:
    python -m tools.ci_tooling.generate.gen_flag_deps            # rewrite the README block
    python -m tools.ci_tooling.generate.gen_flag_deps --check    # CI: exit 1 if stale
"""

import os
import re
import sys

from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
CONFIG_H = os.path.join(ROOT, "src", "protocore_config.h")
README = os.path.join(ROOT, "README.md")
DIAGRAMS = os.path.join(ROOT, "docs", "diagrams")
# Where a clicked flag lands. FEATURES.md headings are feature names, not flags, so this is a
# best-effort anchor; the page itself is the useful destination either way.
FEATURES_URL = "https://github.com/dstroy0/ProtoCore/blob/main/docs/FEATURES.md"

REGION = dr.Region(README, "FLAG DEPS", dr.tool_id(__file__))

ENABLE = re.compile(r"(!?)\s*PC_ENABLE_(\w+)")  # positive (child) / negated (parent) enable flags
# A declared dependency: `#define PC_ENABLE_<CHILD>_NEEDS_<PARENT> PC_ENABLE_<PARENT>`. The name
# carries both ends, so the graph is read from the symbol rather than from an #error's wording.
NEEDS = re.compile(r"^#define\s+PC_ENABLE_([A-Z0-9_]+?)_NEEDS_([A-Z0-9_]+)\s", re.M)
NEG = re.compile(r"!\s*(PC_\w+)")  # any negated PC_* prerequisite (enable flag or resource knob)
NUM = re.compile(r"([A-Z_][A-Z0-9_]*\s*[<>]=?\s*\d+)")  # a numeric side-condition, used as an edge label


def error_message(lines, i):
    """Join the #error statement at line i across backslash continuations into one string."""
    parts = [lines[i].strip()]
    k = i
    while k < len(lines) and lines[k].rstrip().endswith("\\") and k + 1 < len(lines):
        k += 1
        parts.append(lines[k].strip())
    return " ".join(parts)


def parse_guards(text):
    """Return (hard, resource, derived) edges parsed from the header's guards.

    hard:     (parent_flag, child_flag)     - child requires parent (#error "... requires ...")
    resource: (child_flag, label)           - child is PSRAM-class  (#error "... PSRAM ...")
    derived:  (source_flag, derived_flag)   - derived_flag = ... || source_flag || ...
    """
    hard, resource, derived = set(), set(), set()

    # The declared dependencies come first, straight off the symbol names:
    #
    #     #define PC_ENABLE_WS_DEFLATE_NEEDS_WEBSOCKET PC_ENABLE_WEBSOCKET
    #
    # Both sides are IN the name, so this reads the graph without parsing a sentence. That matters:
    # this function used to find edges only by grepping the #error text for the word "requires", and
    # when those messages were reworded to "needs" the graph silently dropped from 38 edges to 6 -
    # a picture that was still generated, still committed, and simply wrong. A dependency stated in
    # prose is a dependency a tool can lose.
    for m in NEEDS.finditer(text):
        child, parent = m.group(1), m.group(2)
        if parent != child:
            hard.add((parent, child))

    stack = []  # active #if / #elif condition strings (innermost last)
    lines = text.splitlines()
    for i, raw in enumerate(lines):
        s = raw.strip()
        if s.startswith("#if ") or s.startswith("#ifdef ") or s.startswith("#ifndef "):
            stack.append(s[4:] if s.startswith("#if ") else "")  # ifdef/ifndef hold no ENABLE terms we track
        elif s.startswith("#elif "):
            if stack:
                stack[-1] = s[6:]
        elif s.startswith("#else"):
            if stack:
                stack[-1] = ""  # an #else branch never holds the deps we extract
        elif s.startswith("#endif"):
            if stack:
                stack.pop()
        elif s.startswith("#error"):
            active = " && ".join(c for c in stack if c)
            children = {m.group(2) for m in ENABLE.finditer(active) if m.group(1) != "!"}
            if not children:
                continue
            msg = error_message(lines, i)
            prereqs = NEG.findall(active)
            # "needs" as well as "requires": the guards that have been converted to a declared
            # _NEEDS_ symbol are already covered above, but the handful still written the old way
            # (the nested `#if A` / `#if !B` form) are only visible here.
            if "requires" in msg or "needs" in msg:
                for prereq in prereqs:
                    # `!PC_ENABLE_X_NEEDS_Y` is the declared symbol being tested, not a parent
                    # feature. Reading it as one invents a node named after the guard itself
                    # ("CIA402 needs CIA402_NEEDS_CANOPEN"); the real edge came from the #define.
                    if prereq.startswith("PC_ENABLE_") and "_NEEDS_" not in prereq:
                        parent = prereq[len("PC_ENABLE_") :]
                        for child in children:
                            if parent != child:
                                hard.add((parent, child))
            elif "PSRAM" in msg:
                m = NUM.search(stack[-1] if stack else "")
                # A raw < / > breaks a Mermaid edge label, so spell the operator out.
                label = m.group(1).replace(" ", "").replace(">", " gt ").replace("<", " lt ") if m else ""
                for prereq in prereqs:
                    if "PSRAM" in prereq and "ACK" not in prereq:
                        for child in children:
                            resource.add((child, label))
        elif s.startswith("#define PC_ENABLE_"):
            # Auto-derived flag: `#define PC_ENABLE_X 1` guarded by an OR of other ENABLE flags.
            active = " || ".join(c for c in stack if c)
            m = re.match(r"#define\s+PC_ENABLE_(\w+)\s+1\b", s)
            if not m or "PC_ENABLE_" not in active:
                continue
            dflag = m.group(1)
            for src in {e for e in re.findall(r"PC_ENABLE_(\w+)", active) if e != dflag}:
                derived.add((src, dflag))
    return hard, resource, derived


def primary_tree(hard):
    """Reduce the hard-dependency DAG to a TREE (exactly one parent edge per child) so the drawn graph
    has no crossing lines. A child with several hard parents keeps an edge only to its "primary" parent
    - the parent with the fewest children, so a hub like TLS does not absorb every variant - and the
    dropped requirements are returned as {secondary_parent: [children]} for a note beside the graph.
    Returns (sorted tree edges, extra dict)."""
    from collections import defaultdict

    parents_of = defaultdict(list)
    child_count = defaultdict(int)
    for p, c in hard:
        parents_of[c].append(p)
        child_count[p] += 1
    tree, extra = [], defaultdict(list)
    for c, ps in parents_of.items():
        prim = min(ps, key=lambda p: (child_count[p], p))
        tree.append((prim, c))
        for p in ps:
            if p != prim:
                extra[p].append(c)
    return sorted(tree), {p: sorted(cs) for p, cs in sorted(extra.items())}


# Light / dark node + edge palettes. A filled node box covers the page background, so the same
# graph reads on GitHub's white or dark canvas; only the two .dot files below differ, one per theme.
PALETTES = {
    "light": {
        "bg": "white",
        "green_fill": "#d6f2e0",
        "green_line": "#2f9e5f",
        "blue_fill": "#e9eafc",
        "blue_line": "#7379e6",
        "text": "#1f2937",
        "edge": "#8a94a3",
        "tls_bg": "#2f9e5f",
        "tls_text": "white",
    },
    "dark": {
        "bg": "#0d1117",
        "green_fill": "#173626",
        "green_line": "#3fb950",
        "blue_fill": "#1c2338",
        "blue_line": "#7c85e8",
        "text": "#e6edf3",
        "edge": "#6b7480",
        "tls_bg": "#238636",
        "tls_text": "#eaffef",
    },
}


def flag_tree(hard):
    """Reduce the hard-dependency graph to the FOREST that gets drawn: TLS is pulled out of the
    tree entirely (every feature that requires it gets an in-node green TLS badge instead of a
    crossing edge to a shared TLS hub), and what remains is run through primary_tree() so each
    child keeps exactly one parent. A forest of single-parent trees is planar, so Graphviz draws
    it with zero crossing edges and each rank left-aligned in its own column.
    Returns (tree_edges, parents, tls_children, nodes)."""
    tls_children = {c for p, c in hard if p == "TLS"}
    non_tls = {(p, c) for p, c in hard if p != "TLS"}
    tree, _extra = primary_tree(non_tls)
    parents = {p for p, _ in tree}
    nodes = {n for pair in hard for n in pair} | {"TLS"}
    return tree, parents, tls_children, nodes


def dot(hard, theme):
    """A Graphviz DOT drawing of the flag forest for one theme. rankdir=LR puts every feature in a
    left-aligned column by depth; the tree is crossing-free by construction; TLS requirements are
    green badges baked into the node label (an HTML-like table cell), never edges."""
    pal = PALETTES[theme]
    tree, parents, tls_children, nodes = flag_tree(hard)

    def node_line(n):
        is_parent = n in parents
        fill = pal["green_fill"] if is_parent else pal["blue_fill"]
        line = pal["green_line"] if is_parent else pal["blue_line"]
        if n in tls_children:
            # HTML-like label: the flag name plus a small green "TLS" chip in the same box, so the
            # "requires TLS" fact attaches to the feature with no edge to cross anything.
            label = (
                '<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="0" CELLPADDING="1"><TR>'
                f'<TD><FONT COLOR="{pal["text"]}">{n}  </FONT></TD>'
                f'<TD BGCOLOR="{pal["tls_bg"]}"><FONT COLOR="{pal["tls_text"]}" POINT-SIZE="8.5">TLS</FONT></TD>'
                "</TR></TABLE>>"
            )
        else:
            label = f'"{n}"'
        # URL + tooltip become a real <a xlink:href> and a <title> child in `dot -Tsvg` output, so
        # the rendered graph is navigable: hover a flag to see what it needs, click to read it.
        # (Graphviz does this natively; mermaid writes the tooltip as an attribute SVG ignores.)
        kids = sorted(c for p, c in tree if p == n)
        needs = sorted(p for p, c in tree if c == n)
        tip = f"PC_ENABLE_{n}"
        if needs:
            tip += " - needs " + ", ".join(needs)
        if kids:
            tip += f" - enables {len(kids)}"
        url = f"{FEATURES_URL}#{n.lower().replace('_', '-')}"
        return (
            f'  {n} [label={label}, fillcolor="{fill}", color="{line}",'
            f' URL="{url}", target="_blank", tooltip="{tip}"];'
        )

    out = [
        "// GENERATED by tools/ci_tooling/generate/gen_flag_deps.py - do not edit by hand.",
        "// Edit src/protocore_config.h (the #error/#if guards) and re-run the generator.",
        "digraph flag_deps {",
        f'  rankdir=LR; bgcolor="{pal["bg"]}";',
        "  graph [nodesep=0.16, ranksep=0.85, splines=true];",
        '  node [shape=box, style="rounded,filled", fontname="monospace", fontsize=11,'
        f' fontcolor="{pal["text"]}", margin="0.11,0.05", penwidth=1.3];',
        f'  edge [color="{pal["edge"]}", penwidth=1.2, arrowsize=0.7];',
        "",
    ]
    for n in sorted(nodes):
        out.append(node_line(n))
    out.append("")
    for p, c in sorted(tree):
        out.append(f"  {p} -> {c};")
    out.append("}")
    return "\n".join(out)


def build_block():
    hard, resource, derived = parse_guards(open(CONFIG_H, encoding="utf-8").read())
    # Pre-render to a PNG (via tools/ci_tooling/assets/render_diagrams.sh) so it shows in the GitHub app + Doxygen too.
    # One Graphviz .dot per theme; Graphviz lays the single-parent forest out crossing-free.
    os.makedirs(DIAGRAMS, exist_ok=True)
    # Dark only. A light/dark pair meant maintaining two sources to serve a theme that is not wanted,
    # and the dark graph carries its own background so it reads the same on either page.
    with open(os.path.join(DIAGRAMS, "flag_deps.dot"), "w", encoding="utf-8", newline="\n") as f:
        f.write(dot(hard, "dark") + "\n")
    for old in ("flag_deps.light.dot", "flag_deps.dark.dot", "flag_deps.light.png", "flag_deps.dark.png"):
        stale_theme = os.path.join(DIAGRAMS, old)
        if os.path.exists(stale_theme):
            os.remove(stale_theme)
    # Drop the previous Mermaid source so a stale flag_deps.mmd is not left to re-render.
    stale = os.path.join(DIAGRAMS, "flag_deps.mmd")
    if os.path.exists(stale):
        os.remove(stale)
    lines = [
        # Markers and the prettier range-ignore are added by dr.apply, which keeps this
        # generator the single authority on the block's markup so --check stays exact.
        "> Generated from the declared `PC_ENABLE_<A>_NEEDS_<B>` symbols in"
        " [src/protocore_config.h](src/protocore_config.h) by `tools/ci_tooling/generate/gen_flag_deps.py` - do not"
        " edit by hand. The picture is an SVG: every node is a link to that feature's entry and carries a"
        " hover tooltip naming what it needs. Graphviz source:"
        " [`docs/diagrams/flag_deps.dot`](docs/diagrams/flag_deps.dot).",
        "",
        "Each **green** node is a parent feature and each **blue** node a child that needs it (a hard"
        " `#error` otherwise) - enable the parent to build the child. A green **`TLS`** badge on a node means"
        " that feature also needs `PC_ENABLE_TLS` (shown as a badge rather than an edge so no lines cross)."
        " **Hover a flag for what it needs; click it to read the feature.** (Auto-derived flags and"
        " PSRAM-class features are listed below the picture rather than drawn as edges, so the graph stays a"
        " clean family forest.)",
        "",
        '<a href="docs/diagrams/flag_deps.svg" title="Open the build-flag dependency graph full size">',
        '  <img alt="Build-flag dependencies" src="docs/diagrams/flag_deps.svg">',
        "</a>",
        "",
    ]
    # Secondary requirements (a child needing a second parent) are not drawn, to keep every tree
    # single-parent and therefore uncrossed; TLS is already handled by the badge above, so exclude it.
    _, extra = primary_tree({(p, c) for p, c in hard if p != "TLS"})
    if extra:
        notes = "; ".join(
            f"**{', '.join('`PC_ENABLE_' + c + '`' for c in cs)}** also need `PC_ENABLE_{p}`" for p, cs in extra.items()
        )
        lines += [f"> Not drawn (so the forest stays uncrossed): {notes}.", ""]
    if derived:
        lines += [
            "<details><summary><b>Auto-derived flags</b> - enabling the left flag turns the right one on"
            " for you; do not set it yourself.</summary>",
            "",
            "| Enabling this... | ...auto-enables |",
            "| --- | --- |",
            *[f"| `PC_ENABLE_{s}` | `PC_ENABLE_{d}` |" for s, d in sorted(derived)],
            "</details>",
            "",
        ]
    if resource:
        lines += [
            "<details><summary><b>PSRAM-class features</b> - the pool cannot fit internal DRAM; enable"
            " `*_IN_PSRAM` or acknowledge with an `*_ACK_DRAM` opt-out.</summary>",
            "",
            "| Feature | Gate |",
            "| --- | --- |",
            *[f"| `PC_ENABLE_{c}` | {lbl if lbl else 'PSRAM pool'} |" for c, lbl in sorted(resource)],
            "</details>",
            "",
        ]
    lines += [
        f"_{len(hard)} hard dependencies, {len(resource)} PSRAM gates, {len(derived)} derived flags._",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    raise SystemExit(dr.apply(README, {REGION: build_block()}, check="--check" in sys.argv[1:]))
