#!/usr/bin/env python3
"""Generate the feature MAP as a compact interactive Mermaid SVG.

A map, not an inventory. The stack on the left, the groups that hang off each layer on the right,
every group carrying its count and a `click` that lands you on that section of the feature list.
Mermaid renders a click as a real <a xlink:href> in the SVG, so the picture is navigation.

The features themselves are deliberately NOT in here. Putting all 254 in produced a 425 KB SVG that
duplicated docs/FEATURES.md into a picture nobody could read - the data lives in FEATURES.md and in
the scroll-box list on the features page, and this points at it.

    python -m tools.ci_tooling.generate.gen_features_tree
    bash tools/ci_tooling/assets/render_diagrams.sh        # -> docs/diagrams/features_map.svg
"""

import os
import re


from tools.ci_tooling.lib import feature_taxonomy as tax
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
OUT = os.path.join(ROOT, "docs", "diagrams", "features_map.mmd")
LIST_URL = "features.html"  # served next to this diagram on the docs site

# Cool -> warm climbing the stack, matching the request-lifecycle diagram so the two agree.
LAYER_HUE = {
    "Foundation": "#8b93a7",
    "Physical & Data Link (L1-L2)": "#f97316",
    "Network (L3)": "#eab308",
    "Transport (L4)": "#f59e0b",
    "Session (L5)": "#10b981",
    "Presentation (L6)": "#3b82f6",
    "Application (L7)": "#6366f1",
}
APP = tax.APPLICATION_LAYER
W = 26  # label column, so every box is the same width


def nid(text):
    return "n_" + re.sub(r"[^A-Za-z0-9]", "_", text)


def pad(text):
    text = text if len(text) <= W else text[: W - 1] + "…"
    return text + " " * (W - len(text))


def main():
    entries = tax.parse_features()

    # group -> count, and layer -> its groups (the application layer is the one that subdivides).
    per_group, per_layer = {}, {}
    for e in entries:
        g = tax.group_of(e["name"])
        per_group[g] = per_group.get(g, 0) + 1
        per_layer.setdefault(tax.layer_of(e["name"]), set()).add(g)

    out = [
        # htmlLabels:false -> native <text>, which is what survives GitHub's sanitizer and keeps the
        # type selectable. Monospace so the padded labels line up into equal-width boxes.
        "%%{init: {'htmlLabels':false,'themeVariables':{"
        "'fontFamily':'monospace','fontSize':'12px','lineColor':'#3b4a5c'},"
        "'flowchart':{'curve':'basis','nodeSpacing':14,'rankSpacing':70,"
        "'padding':8,'useMaxWidth':true,'htmlLabels':false}}}%%",
        "flowchart LR",
        "  %% Auto-generated from docs/FEATURES.md by tools/ci_tooling/generate/gen_features_tree.py.",
    ]

    clicks, styles = [], []
    total = len(entries)

    root = nid("root")
    out.append(f'  {root}["{pad("ProtoCore")}<br/>{pad(f"{total} features")}"]')
    styles.append(f"  style {root} fill:#121924,stroke:#22d3ee,color:#eaf1f8")

    for layer in tax.LAYER_ORDER:
        groups = sorted(per_layer.get(layer, ()))
        if not groups:
            continue
        hue = LAYER_HUE.get(layer, "#6366f1")
        lid = nid(layer)
        count = sum(per_group[g] for g in groups)

        out.append(f'  {lid}["{pad(layer)}<br/>{pad(f"{count} features")}"]')
        out.append(f"  {root} --> {lid}")
        styles.append(f"  style {lid} fill:{hue}22,stroke:{hue},color:#eaf1f8")

        # A layer with exactly one group IS that group; a second identical box teaches nothing.
        if groups == [layer]:
            clicks.append(f'  click {lid} "{LIST_URL}#{tax.github_anchor(layer)}" "{layer}"')
            continue

        for g in groups:
            gid = nid(g)
            out.append(f'  {gid}["{pad(g)}<br/>{pad(f"{per_group[g]}")}"]')
            out.append(f"  {lid} --> {gid}")
            styles.append(f"  style {gid} fill:#121924,stroke:#2b3846,color:#c2ced9")
            clicks.append(f'  click {gid} "{LIST_URL}#{tax.github_anchor(g)}" "{g} - {per_group[g]} features"')

    out.append("")
    out.extend(clicks)
    out.append("")
    out.extend(styles)

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")

    nodes = 1 + len([ly for ly in tax.LAYER_ORDER if per_layer.get(ly)]) + len(per_group)
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {nodes} nodes mapping {total} features")


if __name__ == "__main__":
    main()
