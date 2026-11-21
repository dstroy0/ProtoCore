#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Pull the readable prose out of a document and leave the mathematics behind.
#
#   Usage:  python utils/maint/prose/docs_check --plain [--out FILE] [--min-words N] path [path ...]
#
# WHY THE MATHEMATICS HAS TO COME OUT
#
# A detector scores register. Passed a table of residuals or a line of TeX it returns 1.000 and the
# number means nothing: a column of figures is not written in any register at all. The first sweep
# through this tree proved it - seven of its top eight findings were tables, and the one real
# sentence in the list was buried under them.
#
# So this strips every construct that is notation and retains every construct that is language:
#
#     removed   fenced code, indented code, inline code spans
#     removed   display and inline TeX, tabular bodies, TeX-only lines
#     removed   markdown and TeX table rows, rules, banners
#     removed   any line whose characters are mostly not characters
#     retained      sentences, including sentences that mention a number
#
# THE LAST TWO ARE A DELIBERATE PAIR. "The transform ran in 680 ms" is prose about a measurement and
# stays; "| 4096 | 2006 | 680 |" is the measurement itself and goes. The test is whether the line
# reads as a sentence, not whether it contains a digit, because a rule that dropped every digit
# would drop the majority of the real writing in this tree.
#
# WHAT COMES OUT IS FOR PASTING SOMEWHERE
#
# The output is paragraphs separated by blank lines, with no markers, no numbering and no headings
# left over. That is the form a detector's text box wants, and it is also the form a person reads
# aloud to hear whether a sentence sounds like a person wrote it.
#
# ONE THING IT IS NOT: this makes no judgment. It does not score, flag, or rank. It passes over clean
# text, and the reading happens somewhere else.

import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

from .prose import prose_only

# Display mathematics, in both dialects, taken out whole before anything is read line by line.
DISPLAY = (
    re.compile(r"\$\$.*?\$\$", re.DOTALL),
    re.compile(r"\\\[.*?\\\]", re.DOTALL),
    re.compile(r"\\begin\{(equation|align|gather|multline|tabular|array|matrix|verbatim|lstlisting)"
               r"\*?\}.*?\\end\{\1\*?\}", re.DOTALL),
)

# Fenced and inline code. The fence has to go before the line walk, since its contents can be
# anything at all including text that looks like a sentence.
FENCE = re.compile(r"^\s*(```|~~~)", re.MULTILINE)

INLINE = (
    re.compile(r"`[^`]*`"),                 # markdown code span
    re.compile(r"\$[^$\n]{1,200}\$"),       # inline TeX
    re.compile(r"\\[a-zA-Z]+\*?(\[[^\]]*\])?(\{[^{}]*\})*"),   # a TeX control sequence and its arguments
)

# A line that is structure and not language.
RULE = re.compile(r"^\s*([-=_*#~+.]|\s)+$")
TABLE = re.compile(r"^\s*\|")
TEX_TABLE = re.compile(r"&.*\\\\\s*$")


def characters_fraction(line):
    """What share of the line's non-space characters are characters.

    The single max reliable separator between a sentence and a row of data, and it needs no list
    of notation to maintain. A residual column scores near zero here; a sentence about a residual
    scores high, because English is mostly characters even when it is discussing arithmetic.
    """
    solid = [c for c in line if not c.isspace()]
    if not solid:
        return 0.0
    return sum(1 for c in solid if c.isalpha()) / float(len(solid))


def strip_markers(line):
    """Remove the markup that carries no sound: heading hashes, list bullets, emphasis, comments."""
    line = re.sub(r"^\s*#{1,6}\s*", "", line)
    line = re.sub(r"^\s*[-*+]\s+", "", line)
    line = re.sub(r"^\s*\d+[.)]\s+", "", line)
    line = re.sub(r"^\s*%+\s?", "", line)
    line = re.sub(r"^\s*#\s?", "", line)
    line = line.replace("**", "").replace("__", "")
    # A markdown link retains its text and loses its target, which is a URL and not language.
    line = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", line)
    return line.strip()


def readable(text, min_words):
    """Paragraphs of prose from one document's text, with the notation already removed."""
    for pattern in DISPLAY:
        text = pattern.sub(" ", text)

    out = []
    current = []
    fenced = False
    for raw in text.splitlines():
        if FENCE.match(raw):
            fenced = not fenced
            continue
        if fenced:
            continue

        # An indented block under a paragraph is a code sample or a results table in every file
        # here, and never a sentence that happens to be indented four spaces.
        if raw.startswith("        "):
            continue
        if RULE.match(raw) or TABLE.match(raw) or TEX_TABLE.search(raw):
            if current:
                out.append(" ".join(current))
                current = []
            continue

        line = raw
        for pattern in INLINE:
            line = pattern.sub(" ", line)

        # A LINE THAT LOST MUCH OF ITSELF IS A LINE ABOUT NOTATION, AND IT GOES WHOLE.
        #
        # Removing inline mathematics from a sentence built around it does not leave prose, it
        # leaves a sentence with holes: "the transform of a sequence of length over the integers
        # modulo is" was a real line of this tree's output before this test existed. Passed to a
        # reader or a detector, a line like that is scored for damage this tool did instead of for
        # how it was written.
        #
        # The share removed is the test, because it separates the two cases directly. A sentence
        # MENTIONING a quantity loses a few characters and stays. A sentence CARRYING the quantity
        # loses a fifth of itself and leaves.
        if raw.strip() and (len(line.strip()) / float(len(raw.strip()))) < 0.85:
            if current:
                out.append(" ".join(current))
                current = []
            continue

        line = strip_markers(line)

        if not line:
            if current:
                out.append(" ".join(current))
                current = []
            continue
        if characters_fraction(line) < 0.72:
            continue
        current.append(line)

    if current:
        out.append(" ".join(current))

    retained = []
    for paragraph in out:
        paragraph = " ".join(paragraph.split())
        if len(paragraph.split()) >= min_words:
            retained.append(paragraph)
    return retained


def show_plain():
    parser = argparse.ArgumentParser(description="Prose with the mathematics taken out.")
    parser.add_argument("--out", default=None)
    parser.add_argument("--min-words", type=int, default=12,
                        help="drop anything shorter, which is a caption or a stray label")
    parser.add_argument("--headers", action="store_true",
                        help="for a source file, read its comments and docstrings")
    parser.add_argument("paths", nargs="+")
    given = parser.parse_args()

    chunks = []
    for path in given.paths:
        with io.open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().splitlines()
        if given.headers or path.endswith((".py", ".c", ".h", ".cu", ".cpp")):
            lines = prose_only(path, lines)
        found = readable("\n".join(lines), given.min_words)
        print("  %-58s %3d paragraphs" % (path, len(found)), file=sys.stderr)
        chunks.extend(found)

    body = "\n\n".join(chunks)
    words = len(body.split())
    print("  %d paragraphs, %d words" % (len(chunks), words), file=sys.stderr)

    if given.out:
        with io.open(given.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(body + "\n")
        print("  written to %s" % given.out, file=sys.stderr)
    else:
        print(body)
    return 0
