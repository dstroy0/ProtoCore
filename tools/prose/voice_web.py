#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Build the voice alphabet and the two voice webs from the voice corpus.
#
#   build/tessera_host/tessera_run --processors 1 --name voice_web -- \
#       python utils/maint/prose/voice_web.py
#
# Reads the text voice_pdftotext.sh wrote to build/voice/text/ and the words voice_count.py retained in
# voice.tsv, and writes three tables to utils/maint/prose/. Each holds counts only.
#
#   voice_alphabet.tsv         every character the voice uses, with its count, commonest first.
#                              The rank is the position the engine's web gives it.
#   voice_character_web.tsv    which of the commonest 64 characters follows which, as counts. This
#                              is measure/web.py's web() over the same text with the shares left
#                              as counts: a cell's share is its count over the table's total.
#   voice_word_web.tsv         which voice word follows which, as counts. The pairs are the ones
#                              machine_distance.web_profile counts, over voice.tsv's words only.
#
# All three read the text voice.tsv is counted from, normalized by voice_count.normalized(). Every
# run of whitespace is then read as one space. pdftotext breaks lines where the page did, and a line break is
# the typesetter's.
#
# A pair is never counted across something dropped. In the character web a character past rank 64
# ends the run, as it does in web(). In the word web a word that voice_count.py pulled for review
# ends it. Two words are also a pair only when nothing is between them but spaces, commas,
# quotes, brackets and dashes. A period, a question mark, a colon, a semicolon, a digit or a symbol
# from an equation ends the run, because the words on either side of one were not written as a pair.

import collections
import glob
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import voice_count  # noqa: E402

ROOT = voice_count.ROOT
TEXT = voice_count.TEXT
ALPHABET = os.path.join(ROOT, "utils", "maint", "prose", "voice_alphabet.tsv")
CHARACTER_WEB = os.path.join(ROOT, "utils", "maint", "prose", "voice_character_web.tsv")
WORD_WEB = os.path.join(ROOT, "utils", "maint", "prose", "voice_word_web.tsv")

# The commonest characters the web retains, as deeper_web.py and the language examples read it.
RANKS = 64

SPACES = re.compile(r"\s+")
# Between two words, the gap that retains them a pair: spaces, commas, quotes, dashes and brackets.
JOINING_GAP = re.compile(r"[\s,\"'()\[\]\-–]*")


def displayed(symbol):
    """A character as a TSV cell: itself where it prints, its code point where it does not."""
    if symbol.isprintable() and not symbol.isspace():
        return symbol
    return "U+%04X" % ord(symbol)


def texts():
    sources = sorted(glob.glob(os.path.join(TEXT, "*.txt")))
    if not sources:
        sys.exit("no text in %s; run voice_pdftotext.sh under tessera_run first" % TEXT)
    for path in sources:
        with io.open(path, encoding="utf-8", errors="replace") as handle:
            yield SPACES.sub(" ", voice_count.normalized(handle.read()))


def retained_words():
    with io.open(voice_count.OUT, encoding="utf-8") as handle:
        next(handle)
        return {line.split("\t", 1)[0] for line in handle}


def main():
    retained = retained_words()
    characters = collections.Counter()
    bodies = []
    for body in texts():
        characters.update(body)
        bodies.append(body)

    ordered = sorted(characters, key=lambda symbol: (-characters[symbol], symbol))
    seat = {symbol: place for place, symbol in enumerate(ordered[:RANKS])}
    character_pairs = collections.Counter()
    word_pairs = collections.Counter()
    for body in bodies:
        previous = None
        for symbol in body:
            place = seat.get(symbol)
            if (place is not None) and (previous is not None):
                character_pairs[(previous, place)] += 1
            previous = place

        last = None
        last_end = 0
        for match in voice_count.WORD.finditer(body):
            word = match.group(0).lower()
            if word not in retained:
                last = None
                continue
            if (last is not None) and JOINING_GAP.fullmatch(body, last_end, match.start()):
                word_pairs[(last, word)] += 1
            last = word
            last_end = match.end()

    with io.open(ALPHABET, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("rank\tsymbol\tcode\tcount\n")
        for place, symbol in enumerate(ordered):
            handle.write("%d\t%s\tU+%04X\t%d\n" % (place, displayed(symbol), ord(symbol), characters[symbol]))
    with io.open(CHARACTER_WEB, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("from_rank\tto_rank\tfrom\tto\tcount\n")
        for (first, second), count in sorted(character_pairs.items(), key=lambda item: (-item[1], item[0])):
            handle.write("%d\t%d\t%s\t%s\t%d\n" % (first, second, displayed(ordered[first]), displayed(ordered[second]), count))
    with io.open(WORD_WEB, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("first\tsecond\tcount\n")
        for (first, second), count in sorted(word_pairs.items(), key=lambda item: (-item[1], item[0])):
            handle.write("%s\t%s\t%d\n" % (first, second, count))

    past = sum(characters[symbol] for symbol in ordered[RANKS:])
    print("  alphabet: %d characters, %d symbols, %d past rank %d, written to %s"
          % (sum(characters.values()), len(ordered), past, RANKS, os.path.relpath(ALPHABET, ROOT)))
    print("  character web: %d pairs in %d of %d cells, written to %s"
          % (sum(character_pairs.values()), len(character_pairs), RANKS * RANKS,
             os.path.relpath(CHARACTER_WEB, ROOT)))
    print("  word web: %d pairs, %d distinct, %d seen once, written to %s"
          % (sum(word_pairs.values()), len(word_pairs), sum(1 for count in word_pairs.values() if count == 1),
             os.path.relpath(WORD_WEB, ROOT)))


if __name__ == "__main__":
    main()
