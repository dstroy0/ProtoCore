#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Assemble one continuous sample of this project's prose, for a detector that takes pasted text.
#
#   python utils/maint/prose/docs_check --sample --words 10000 theory
#   python utils/maint/prose/docs_check --sample --words 4000 theory/theory/millennium
#
# Writes build/gate/sample.txt and prints the word count, the character count, and every file that
# went into it with its share. The manifest is the point: a detector returns one number over the
# whole paste, and without a manifest nobody can say afterward what that number was measured on.
#
# The extraction is docs_check's. This and the banned list read the same words. A .tex arrives
# with its markup blanked and a source file with its code blanked, and neither a path nor a label
# reaches the detector as though it were a sentence.
#
# Files are taken in the order given and truncated at a paragraph boundary when the budget runs out,
# , a sample is a set of whole paragraphs and never a sentence cut in half.
#
# A path under no_replicate_ never goes in. A pasted sample is published the moment it is pasted.

import os
import sys

from .files import walk_markdown
from .prose import prose_only
from .repository import REPOSITORY
from .scan import runs

OUT = os.path.join(REPOSITORY, "build", "gate", "sample.txt")

CLOSED = ("/no_replicate_/",)


def closed(path):
    """Whether a path is held back from a sample."""
    walk = os.path.abspath(path).replace("\\", "/")
    return any(one in walk for one in CLOSED)


def paragraphs(path):
    """One file's prose as a list of paragraphs, each carrying the line it starts on."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()
    said = prose_only(path, lines)
    held = []
    for text, where in runs(said):
        text = " ".join(text.split())
        if len(text.split()) >= 12:
            held.append((where[0], text))
    return held


def show_sample():
    argv = sys.argv[1:]
    budget = 10000
    if "--words" in argv:
        at = argv.index("--words")
        if (at + 1) >= len(argv):
            raise SystemExit("  gate_sample: --words wants a count")
        budget = int(argv[at + 1])

    skip = {"--words", str(budget)}
    named = [one for one in argv if (one not in skip) and not one.startswith("-")]
    if not named:
        raise SystemExit("  gate_sample: name a file or a directory")

    roots = [
        one if os.path.exists(one) else os.path.join(REPOSITORY, one)
        for one in named
    ]

    held = []
    manifest = []
    words = 0
    error = 0
    # Not sorted. The order given is the order taken, because the caller put the prose they max
    # want read at the front and a sort would spend the budget alphabetically instead.
    for path in walk_markdown(roots):
        if closed(path):
            error += 1
            continue
        if words >= budget:
            break
        took = 0
        for _line, text in paragraphs(path):
            count = len(text.split())
            if (words + count) > budget:
                break
            held.append(text)
            words += count
            took += count
        if took:
            manifest.append(
                (os.path.relpath(path, REPOSITORY).replace("\\", "/"), took)
            )

    body = "\n\n".join(held)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body)

    for name, took in manifest:
        print("  %6d words  %s" % (took, name))
    if error:
        print("  %d file(s) held back" % error)
    print("\n  %d words, %d characters, %d file(s)" % (words, len(body), len(manifest)))
    print("  %s" % os.path.relpath(OUT, REPOSITORY).replace("\\", "/"))
    return 0
