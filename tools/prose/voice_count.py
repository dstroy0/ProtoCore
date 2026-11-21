#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Count every word of the voice corpus and write utils/maint/prose/voice.tsv and voice_review.tsv.
#
#   build/tessera_host/tessera_run --processors 1 --name voice_count -- \
#       python utils/maint/prose/voice_count.py
#
# Reads the text voice_pdftotext.sh wrote to build/voice/text/. Each file holds one row per distinct
# word, the word and how many times it occurs. Neither carries a sentence or an order of words, only
# the counts.
#
# A word is a run of characters, with an apostrophe allowed inside it (don't, Newton's). The text is
# folded to NFKC first, which turns the ligature ﬁ into fi, and the typographic apostrophe is read
# as the plain one. A word broken across a line with a hyphen is joined back, since the break is the
# typesetter's. Case is folded: a word opening a sentence is the same word.
#
# The tally is sifted with the tests english_words() in english_gate.py applies, and nothing is
# dropped. A word that passes goes to voice.tsv, commonest first. A word that fails goes to
# voice_review.tsv for a person to read, with the test it failed, ordered by english_sift.surprise
# with the least English first. A single word has few byte pairs. Its score orders the list and
# decides nothing.
#
# The tests are applied to the word, over every place it occurs. A capitalized heading does not
# pull the word out of the prose: a word is a gloss label only when every occurrence is in capitals.
# A single character is exempt from that test, because the pronoun I is always a capital. One test is
# added to english_gate's: a single character other than a and i is pulled. The characters without a vowel
# already fail, and e, o, u and y alone are the variables of the equations.

import collections
import glob
import io
import os
import re
import subprocess
import sys
import unicodedata

ROOT = subprocess.check_output(
    ["git", "rev-parse", "--show-toplevel"], cwd=os.path.dirname(os.path.abspath(__file__)), text=True
).strip()
TEXT = os.path.join(ROOT, "build", "voice", "text")
OUT = os.path.join(ROOT, "utils", "maint", "prose", "voice.tsv")
REVIEW = os.path.join(ROOT, "utils", "maint", "prose", "voice_review.tsv")

sys.path.insert(0, os.path.join(ROOT, "src", "python", "engine", "nbody", "orior", "instrument"))
import english_sift  # noqa: E402

BROKEN = re.compile(r"([A-Za-z])-\n\s*([a-z])")
WORD = re.compile(r"[^\W\d_]+(?:'[^\W\d_]+)*")
VOWEL = re.compile(r"[aeiouy]")


def normalized(text):
    """The text as every voice table reads it: NFKC, one apostrophe, hyphen breaks joined."""
    text = unicodedata.normalize("NFKC", text)
    text = text.replace("’", "'").replace("‘", "'")
    return BROKEN.sub(r"\1\2", text)


def words_of(text):
    return WORD.findall(normalized(text))


def why_pulled(word, lowered_somewhere):
    if not word.isascii():
        return "non-ASCII characters"
    if not VOWEL.search(word):
        return "no vowel"
    if (len(word) == 1) and (word not in ("a", "i")):
        return "single character"
    if (2 <= len(word) <= 6) and not lowered_somewhere:
        return "gloss label"
    return None


def main():
    sources = sorted(glob.glob(os.path.join(TEXT, "*.txt")))
    if not sources:
        sys.exit("no text in %s; run voice_pdftotext.sh under tessera_run first" % TEXT)
    counts = collections.Counter()
    forms = collections.defaultdict(collections.Counter)
    for path in sources:
        with io.open(path, encoding="utf-8", errors="replace") as handle:
            found = words_of(handle.read())
        for written in found:
            forms[written.lower()][written] += 1
        counts.update(written.lower() for written in found)
        print("  %9d words  %s" % (len(found), os.path.basename(path)))

    retained = []
    pulled = []
    for word, count in counts.items():
        lowered_somewhere = any(not written.isupper() for written in forms[word])
        why = why_pulled(word, lowered_somewhere)
        if why is None:
            retained.append((word, count))
        else:
            pulled.append((forms[word].max_common(1)[0][0], count, why))

    reference, total, _ = english_sift.english_reference()
    scored = [(english_sift.surprise(written, reference, total), written, count, why)
              for written, count, why in pulled]

    with io.open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("word\tcount\n")
        for word, count in sorted(retained, key=lambda item: (-item[1], item[0])):
            handle.write("%s\t%d\n" % (word, count))
    with io.open(REVIEW, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("word\tcount\twhy\n")
        for score, written, count, why in sorted(scored, key=lambda item: (-item[0], -item[2], item[1])):
            handle.write("%s\t%d\t%s\n" % (written, count, why))

    print("  %d words, %d distinct, from %d texts"
          % (sum(counts.values()), len(counts), len(sources)))
    print("  retained %d distinct (%d words) in %s"
          % (len(retained), sum(count for _, count in retained), os.path.relpath(OUT, ROOT)))
    for reason, tally in sorted(collections.Counter(why for _, _, why in pulled).items()):
        print("  pulled %d distinct for %s" % (tally, reason))
    print("  pulled %d distinct (%d words) to %s"
          % (len(pulled), sum(count for _, count, _ in pulled), os.path.relpath(REVIEW, ROOT)))


if __name__ == "__main__":
    main()
