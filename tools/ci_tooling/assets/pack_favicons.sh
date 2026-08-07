#!/usr/bin/env bash
# Package a generated favicon SVG (web_assets/favicons/<name>.svg) into a drop-in favicon set tarball:
# favicon-{16,32,48,180,192,512}.png + apple-touch-icon.png + favicon.ico + favicon.svg +
# site.webmanifest, as docs/favicons/dist/<name>.tar.gz - unpack it into a web root and it just works.
#
# The tarballs are build outputs (not committed); generate the one(s) you want on demand, or --all in
# CI as a release artifact. Needs rsvg-convert (librsvg2-bin) for crisp SVG rasterization + ImageMagick
# (convert) for the multi-size .ico.
#
#   tools/ci_tooling/assets/pack_favicons.sh bolt-indigo     # one
#   tools/ci_tooling/assets/pack_favicons.sh --all            # every favicon
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/src/web/favicons"
DIST="$ROOT/docs/favicons/dist"

pack() {
    local name="$1" svg="$SRC/$1.svg"
    [ -f "$svg" ] || {
        echo "no such favicon: $1" >&2
        return 1
    }
    local tmp
    tmp="$(mktemp -d)"
    for s in 16 32 48 180 192 512; do rsvg-convert -w "$s" -h "$s" "$svg" -o "$tmp/favicon-$s.png"; done
    cp "$tmp/favicon-180.png" "$tmp/apple-touch-icon.png"
    convert "$tmp/favicon-16.png" "$tmp/favicon-32.png" "$tmp/favicon-48.png" "$tmp/favicon.ico"
    cp "$svg" "$tmp/favicon.svg"
    printf '{ "icons": [ { "src": "favicon-192.png", "sizes": "192x192", "type": "image/png" }, { "src": "favicon-512.png", "sizes": "512x512", "type": "image/png" } ] }\n' >"$tmp/site.webmanifest"
    mkdir -p "$DIST"
    tar czf "$DIST/$name.tar.gz" -C "$tmp" .
    rm -rf "$tmp"
    echo "packed $DIST/$name.tar.gz"
}

if [ "${1:-}" = "--all" ]; then
    for f in "$SRC"/*.svg; do pack "$(basename "$f" .svg)"; done
else
    pack "${1:?usage: pack_favicons.sh <name> | --all}"
fi
