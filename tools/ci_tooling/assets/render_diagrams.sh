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
#   tools/ci_tooling/assets/render_diagrams.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")/../../../docs/diagrams" && pwd)"
PPTR="$(mktemp)"
printf '{"args":["--no-sandbox","--disable-gpu"]}' >"$PPTR"

# maxTextSize / maxEdges cannot be raised from an %%{init}%% directive - mermaid treats them as
# secured options and silently renders "Maximum text size in diagram exceeded" instead. They have to
# come from a config file, so every diagram here gets one.
MMCFG="$(mktemp)"
printf '{"maxTextSize":500000,"maxEdges":2000}' >"$MMCFG"

trap 'rm -f "$PPTR" "$MMCFG"' EXIT
shopt -s nullglob

# Skip re-rendering an unchanged diagram: rasterization is not byte-deterministic, so re-rendering an
# unchanged source would emit phantom PNG churn every CI run - a needless commit that also makes the
# auto-commit race for main. The sources ARE deterministic (Python), so "source unchanged vs git AND the
# PNG(s) exist" means the committed PNGs are already current.
for mmd in "$DIR"/*.mmd; do
    name="$(basename "$mmd" .mmd)"
    dark="$DIR/$name.dark.png"
    svg="$DIR/$name.svg"
    # The guard tests the files this actually produces. It used to test a light PNG that is no
    # longer emitted, which made "unchanged" impossible and re-rendered every run - the phantom
    # churn this skip exists to prevent.
    if [ -f "$svg" ] && [ -f "$dark" ] && git diff --quiet HEAD -- "$mmd" 2>/dev/null; then
        echo "unchanged $name (skip render)"
        continue
    fi
    width=1100

    # ONE theme, dark, with the background baked in rather than transparent. A transparent dark
    # diagram is unreadable the moment it lands on a light page, and a light/dark <picture> pair
    # means maintaining two of everything to serve a theme nobody wants. A self-contained dark card
    # reads the same on GitHub light, GitHub dark, the mobile app, and Doxygen.
    #
    # SVG is what the docs and README embed: the labels are native <text> (htmlLabels:false in the
    # source), so GitHub's sanitizer leaves them alone - the foreignObject problem that forced
    # rasters here originally. Vector keeps the type selectable and sharp at any zoom, and cannot be
    # clipped by a font mismatch between measuring and drawing. The PNG stays for anywhere a raster
    # is still required.
    mmdc -c "$MMCFG" -p "$PPTR" -i "$mmd" -o "$dark" -t dark -b "#0d1117" -w "$width" --scale 2
    mmdc -c "$MMCFG" -p "$PPTR" -i "$mmd" -o "$DIR/$name.svg" -t dark -b "#0d1117" -w "$width"

    # Mermaid writes a click's tooltip as a title ATTRIBUTE, which SVG ignores - the tooltip only
    # appears if it is a <title> CHILD. This adds them, so hovering a node in the finished picture
    # actually says something.
    python3 "$(dirname "$0")/svg_tooltips.py" "$DIR/$name.svg" >/dev/null

    rm -f "$DIR/$name.light.png" "$DIR/$name.light.svg" "$DIR/$name.dark.svg"
    echo "rendered $name (dark; svg + png)"
done

# Graphviz: to SVG, not PNG. dot turns a node's URL= and tooltip= into a real <a xlink:href> and a
# <title> child, so the dependency graph is navigable and hover says what a flag needs - natively,
# with none of the post-processing mermaid's tooltips require. The forest layout is why this one
# stays Graphviz: it guarantees the crossing-free, left-aligned single-parent tree dagre cannot.
for src in "$DIR"/*.dot; do
    stem="$(basename "$src" .dot)"
    svg="$DIR/$stem.svg"
    if [ -f "$svg" ] && git diff --quiet HEAD -- "$src" 2>/dev/null; then
        echo "unchanged $stem (skip render)"
        continue
    fi
    dot -Tsvg "$src" -o "$svg"
    echo "rendered $stem"
done
