#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the README feature/codec tables from docs/FEATURES.md.

docs/FEATURES.md is the single source of truth: every feature is a `## Name`
heading, an optional `` `PC_ENABLE_*` `` flag line, and a description
paragraph. This script parses those entries, splits them into a FEATURES table
and a CODECS table, and writes both (as HTML tables with a merged header) into
the marked region of README.md and docs/README.md. The hover tooltip is the
feature's description; the link target is the FEATURES.md anchor.

Run from the repo root:
    python ci_tooling/generate/gen_feature_tables.py            # rewrite the tables
    python ci_tooling/generate/gen_feature_tables.py --check    # CI: fail if stale

Keeping the tables generated means they can never drift from FEATURES.md (a
hand-maintained grid silently lost 28 of 106 features before this existed).
"""

import os
import re
import sys

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))

import feature_taxonomy as tax
from feature_taxonomy import (
    APPLICATION_LAYER,
    CATEGORY_MEMBERS,
    CATEGORY_ORDER,
    LAYER_MEMBERS,
    LAYER_ORDER,
    category_of,
    github_anchor,
    html_escape,
    layer_of,
)

import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
FEATURES_MD = os.path.join(ROOT, "docs", "FEATURES.md")
CONFIG_H = os.path.join(ROOT, "src", "protocore_config.h")

# Internal derived flags: auto-set from other flags, not user-facing opt-ins, so they
# get no FEATURES.md entry of their own. Every other PC_ENABLE_* must be documented
# (the coverage guard below fails CI otherwise - this is how the whole industrial-protocol
# wave once drifted out of the feature grid unnoticed).
INTERNAL_FLAGS = {
    "PC_ENABLE_STREAM_BODY",  # = OTA || UPLOAD || WEBDAV (shared parser machinery)
    "PC_ENABLE_CLIENT_TLS",  # = HTTP_CLIENT_TLS || MQTT_TLS || WS_CLIENT_TLS || EDGE_ORIGIN_TLS
}

COLUMNS = 5


# Where each target file's links to FEATURES.md point.
TARGETS = {
    os.path.join(ROOT, "README.md"): "docs/FEATURES.md",
    os.path.join(ROOT, "docs", "README.md"): "FEATURES.md",
}


def render_table(title, rows, link_prefix):
    """Render one HTML table, uniform in width across every table and left-justified.

    GitHub's Markdown renderer keeps none of a stylesheet - only presentational attributes
    survive - so uniform, aligned columns have to be baked into the HTML: every table is
    `width="100%"`, every cell is a fixed `width="20%"` (COLUMNS = 5 => equal fifths), and the
    short last row is padded with empty cells so all five columns line up instead of the
    remaining cells stretching to fill. Cells are `align="left"` so the flags read as a tidy
    left-aligned grid rather than drifting to center. The `feature-table` class + `td:empty`
    rule in the docs site (docs/custom.css) hide the padding cells and theme the borders on top.
    """
    colw = f"{100 // COLUMNS}%"
    out = [
        '<table class="feature-table" width="100%">',
        f'<thead><tr><th colspan="{COLUMNS}" align="left">{html_escape(title)}</th></tr></thead>',
        "<tbody>",
    ]
    for r in range(0, len(rows), COLUMNS):
        chunk = rows[r : r + COLUMNS]
        out.append("<tr>")
        for name, desc in chunk:
            href = f"{link_prefix}#{github_anchor(name)}"
            out.append(
                f'  <td align="left" width="{colw}"><a href="{html_escape(href)}"'
                f' title="{html_escape(desc)}">{html_escape(name)}</a></td>'
            )
        for _ in range(COLUMNS - len(chunk)):  # pad so every table keeps five aligned columns
            out.append(f'  <td align="left" width="{colw}"></td>')
        out.append("</tr>")
    out += ["</tbody>", "</table>"]
    return "\n".join(out)


def build_block(link_prefix):
    entries = [(e["name"], e["desc"]) for e in tax.parse_features(FEATURES_MD)]
    # Validate the layer map so a renamed/removed heading is caught, not silently dropped.
    known = {e[0] for e in entries}
    mapped = set().union(*LAYER_MEMBERS.values())
    missing = mapped - known
    if missing:
        raise SystemExit(f"LAYER_MEMBERS headings not found in FEATURES.md: {sorted(missing)}")
    # Validate the L7 category map the same way: every listed heading must exist, and must be an
    # application-layer feature (a heading pinned to a lower layer must not also be categorized).
    cat_mapped = set().union(*CATEGORY_MEMBERS.values())
    cat_missing = cat_mapped - known
    if cat_missing:
        raise SystemExit(f"CATEGORY_MEMBERS headings not found in FEATURES.md: {sorted(cat_missing)}")
    cat_wrong_layer = {n for n in cat_mapped if layer_of(n) != APPLICATION_LAYER}
    if cat_wrong_layer:
        raise SystemExit(f"CATEGORY_MEMBERS entries are not Application-layer features: {sorted(cat_wrong_layer)}")

    by_layer = {layer: [] for layer in LAYER_ORDER}
    for name, desc in entries:
        by_layer[layer_of(name)].append((name, desc))

    # A SUMMARY, not the inventory. The full per-feature tables lived here and ran to 526 lines of
    # HTML with every description inlined into a title= attribute - more than half the README, for a
    # list nobody scrolls. The complete set is one click away on the docs site (searchable, and
    # grouped the same way), and docs/FEATURES.md remains the source both are generated from.
    total = len(entries)
    # link_prefix is the path TO FEATURES.md ("docs/FEATURES.md" from the repo root, "FEATURES.md"
    # from inside docs/), so the directory the diagrams sit in has to come off it rather than being
    # assumed - the two targets are at different depths.
    docs_dir = link_prefix[: -len("FEATURES.md")]
    rows = []
    for layer in LAYER_ORDER:
        names = sorted(by_layer[layer], key=lambda e: e[0].lower())
        if not names:
            continue
        sample = ", ".join(n for n, _ in names[:4])
        rows.append(f"| **{layer}** | {len(names)} | {sample}{', …' if len(names) > 4 else ''} |")

    return "\n".join(
        [
            f"**{total} features**, every one a compile-time `PC_ENABLE_*` flag that is off unless you ask"
            " for it. Core HTTP/1.1 parsing, routing, middleware, JSON, templating and chunked responses are"
            " always on and are not flags.",
            "",
            '<a href="https://dstroy0.github.io/ProtoCore/features.html" title="Browse every feature">',
            '  <img alt="Feature map: the OSI stack and the feature groups on each layer"'
            f' src="{docs_dir}diagrams/features_map.svg" width="100%">',
            "</a>",
            "",
            "| Layer | Features | For example |",
            "| --- | --- | --- |",
            *rows,
            "",
            "**[Browse all of them →](https://dstroy0.github.io/ProtoCore/features.html)** - filterable, grouped"
            f" by layer, one line each. Full descriptions live in [FEATURES.md]({link_prefix});"
            " both are generated from it, so they cannot drift.",
        ]
    )


def check_flag_coverage():
    """Fail if a PC_ENABLE_* flag in the config header has no FEATURES.md entry
    (excluding the internal derived flags). Guards against a shipped feature silently
    never reaching the feature grid."""
    cfg = open(CONFIG_H, "r", encoding="utf-8").read()
    feat = open(FEATURES_MD, "r", encoding="utf-8").read()
    defined = set(re.findall(r"^#define (PC_ENABLE_[A-Z0-9_]+) 0", cfg, re.M))
    documented = set(re.findall(r"^`(PC_ENABLE_[A-Z0-9_]+)`", feat, re.M))
    missing = sorted(defined - documented - INTERNAL_FLAGS)
    if missing:
        raise SystemExit(
            "FEATURES.md is missing entries for these PC_ENABLE_* flags "
            "(add a `## Name` section, or list the flag in INTERNAL_FLAGS if it is "
            f"internal): {missing}"
        )


def main():
    check = "--check" in sys.argv[1:]
    check_flag_coverage()
    rc = 0
    for path, prefix in TARGETS.items():
        region = dr.Region(path, "FEATURE TABLES", dr.tool_id(__file__))
        rc |= dr.apply(path, {region: build_block(prefix)}, check=check)
    if rc:
        sys.exit(1)


if __name__ == "__main__":
    main()
