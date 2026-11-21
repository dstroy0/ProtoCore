#!/usr/bin/env bash
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Convert the voice corpus to text, eight PDFs at a time, as one host job.
#
#   build/tessera_host/tessera_run --processors 8 --name voice_pdftotext -- \
#       bash utils/maint/prose/voice_pdftotext.sh D:/voice
#
# The text goes to build/voice/text/ and never into the tree: it is the books' text. The tree
# retains voice.tsv, the words and their counts, which voice_count.py writes from it.
#
# Each title is converted from one PDF. The folder holds scans of the same lectures beside the
# editions with a text layer, and a title counted twice would count every word in it twice. The
# scans are left out, and so are the two excerpt volumes (Six Easy Pieces and Six Not-So-Easy
# Pieces reprint chapters of the Lectures), the older edition of Feynman's Tips, the second version
# of the Volume 2 PDF, and the Lost Lecture, whose text is the Goodsteins'.
#
# The Lectures on Computation are left out as well. Their fonts carry no map to Unicode, and every
# extractor tried (pdftotext, pdftotext -raw, pypdf, pypdfium2) gave 34 to 40 percent of the words
# in accented characters that are for other characters, with the characters between them misread too.

set -u

SOURCE="${1:?usage: voice_pdftotext.sh <voice folder>}"
ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
OUT="$ROOT/build/voice/text"
mkdir -p "$OUT"

LECTURES="Richard Feynman - The Feynman Lectures on Physics (PDF, DJVU, ENG)"
TITLES=(
    "QED The Strange Theory of Light and Matter/QED_ The Strange Theory of Ligh - Richard P. Feynman.pdf"
    "$LECTURES/Richard Feynman - The Feynman Lectures on Physics - Volume 1 (New Millennium, 2010).pdf"
    "$LECTURES/Richard Feynman - The Feynman Lectures on Physics - Volume 2 (New Millennium, 2010).pdf"
    "$LECTURES/Richard Feynman - The Feynman Lectures on Physics - Volume 3 (New Millennium, 2010).pdf"
    "$LECTURES/Extra/Richard Feynman - Feynman Lectures on Gravitation (Addison-Wesley, 1995).pdf"
    "$LECTURES/Richard Feynman - Feynman's Tips on Physics (Basic Books, 2013).pdf"
)

missing=0
for title in "${TITLES[@]}"; do
    if [ ! -f "$SOURCE/$title" ]; then
        echo "  missing: $SOURCE/$title"
        missing=1
    fi
done
[ "$missing" -eq 0 ] || exit 1

# -enc UTF-8 retains the typographic quotes and the ligatures as characters. -nopgbrk leaves out the
# form feed between pages, which would otherwise glue a page's last word to the next page's first.
# -raw retains the text in the order the page draws it. Without it pdftotext positions the text out by
# position and splits a word wherever the glyphs are spaced apart: in QED that gave "p hoton" and
# "throug h", and 94.78 percent of its words were known to the other texts against 98.43 with -raw.
rm -f "$OUT"/*.txt
for title in "${TITLES[@]}"; do
    printf '%s\0' "$title"
done | xargs -0 -P 8 -I '{}' bash -c \
    'pdftotext -enc UTF-8 -nopgbrk -raw "$0/$1" "$2/$(basename "$1" .pdf).txt" && echo "  converted: $(basename "$1")"' \
    "$SOURCE" '{}' "$OUT"
