#!/usr/bin/env bash
# Render each diagram source in docs/diagrams/ to a light + dark PNG. The READMEs / docs embed these
# PNGs through a <picture> element (instead of a live fence) so the diagrams show in the GitHub web UI,
# the GitHub mobile app, AND Doxygen - the app and Doxygen do not render mermaid, and GitHub strips the
# foreignObject an inline SVG would need for the multi-line labels. A PNG bakes the labels into pixels.
#
# Two source formats:
#   *.mmd                       - Mermaid (gen_api_flow.py); one source -> light + dark via mmdc themes.
#   *.light.dot / *.dark.dot    - Graphviz (gen_flag_deps.py); a per-theme source -> the matching PNG.
#     The flag graph is a single-parent forest, which Graphviz lays out crossing-free and left-aligned -
#     Mermaid's dagre could not guarantee either. Needs the `dot` binary (apt: graphviz).
# Needs mermaid-cli (mmdc) + a Chromium, and graphviz (dot). In CI, install all; locally, run on a host
# that has them.
#
#   ci_tooling/assets/render_diagrams.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")/../../docs/diagrams" && pwd)"
PPTR="$(mktemp)"
printf '{"args":["--no-sandbox","--disable-gpu"]}' >"$PPTR"
trap 'rm -f "$PPTR"' EXIT
shopt -s nullglob

# Skip re-rendering an unchanged diagram: rasterization is not byte-deterministic, so re-rendering an
# unchanged source would emit phantom PNG churn every CI run - a needless commit that also makes the
# auto-commit race for main. The sources ARE deterministic (Python), so "source unchanged vs git AND the
# PNG(s) exist" means the committed PNGs are already current.
for mmd in "$DIR"/*.mmd; do
    name="$(basename "$mmd" .mmd)"
    light="$DIR/$name.light.png"
    dark="$DIR/$name.dark.png"
    if [ -f "$light" ] && [ -f "$dark" ] && git diff --quiet HEAD -- "$mmd" 2>/dev/null; then
        echo "unchanged $name (skip render)"
        continue
    fi
    width=1100
    mmdc -p "$PPTR" -i "$mmd" -o "$light" -t default -b white -w "$width" --scale 2
    mmdc -p "$PPTR" -i "$mmd" -o "$dark" -t dark -b "#0d1117" -w "$width" --scale 2
    echo "rendered $name (light + dark)"
done

# Graphviz: each *.light.dot / *.dark.dot already carries its own theme colors, so it maps 1:1 to a PNG.
for src in "$DIR"/*.light.dot "$DIR"/*.dark.dot; do
    stem="$(basename "$src" .dot)" # e.g. flag_deps.light
    png="$DIR/$stem.png"
    if [ -f "$png" ] && git diff --quiet HEAD -- "$src" 2>/dev/null; then
        echo "unchanged $stem (skip render)"
        continue
    fi
    dot -Tpng -Gdpi=140 "$src" -o "$png"
    echo "rendered $stem"
done
