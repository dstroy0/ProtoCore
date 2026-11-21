#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Retain the English of a mixed corpus and drop the rest, leaving a comparison that is about English.
#
#   Usage:  from english_gate import english_only, english_words
#           python utils/maint/prose/english_gate.py            (reports what the gate removes)
#
# WHY THIS EXISTS, AND WHAT IT CORRECTS
#
# Three measurements in this directory used the 154 research papers under build/papers as their
# human English pole. Those papers are linguistics: every one carries Salishan orthography,
# interlinear glosses, IPA and tables of forms, and a large share of their tokens are not English at
# all. Comparing anything against that corpus measures how much language data a text prints before
# it measures how anybody writes.
#
# It showed up as a result each time and was explained away instead of fixed:
#
#   prose_distance      reported this repository as closer to English than the papers, and the
#                       reason was that the papers are not English.
#   machine_distance   put the Salishan extraction scripts furthest from the machine pole. They
#                       were matching the papers on Salishan instead of on register.
#   ban_evidence        divided phrase counts by a word total padded with non-English tokens.
#                       Every per-100k rate it reported was low.
#
# prose_era did not have the fault, because it counted ASCII words only. This is that fix, taken out
# of one file and made shared, and applied to both sides of every comparison.
#
# THE GATE IS TWO STAGES AND NEEDS BOTH
#
# Line stage. A line has to look like writing and has to score inside the English cut on the bench
# that already measures English. That removes a page of Mandarin and a page of shifted font codes,
# which are both un-English and only one of them is worth knowing about.
#
# Word stage. What the line stage leaves is cut to runs of ASCII characters. A gloss line reading COP=3SBJ
# D/C=NMLZ=STAT passes the line stage, because it is written in characters and is short, and it is not
# English. Restricting to ASCII words drops the orthography, the IPA and the Greek outright, and the
# stop lists in the callers drop the language names that survive as ASCII.

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

sys.path.insert(0, os.path.join(ROOT, "src", "python", "engine", "nbody", "orior", "instrument"))

from english_sift import english_reference, looks_like_writing, surprise  # noqa: E402

WORD = re.compile(r"[A-Za-z][A-Za-z']*")

# An English word carries a vowel. A romanized Salishan form very often does not.
VOWEL = re.compile(r"[aeiouy]")

# A line shorter than this is not scored. A three word line carries too few byte pairs for a mean
# to mean anything, and dropping them all would take the headings a document needs.
LEAST = 40

# How much worse than the reference's own median a line may score before it is dropped. Calibrated
# against cc_english below, and printed by the report at the foot of this file.
SLACK = 1.35

_reference = None


def reference():
    """The English byte pair reference, built once and retained."""
    global _reference
    if _reference is None:
        counts, total, lines = english_reference()
        _reference = (counts, total, lines)
    return _reference


def cut_for(counts, total, sample):
    """The surprise a line may reach before it stops counting as English.

    Taken from the reference corpus itself: the median line of ordinary English, times SLACK. A cut
    picked by hand would be a number nobody could defend; this one moves with the reference.
    """
    scores = []
    for line in sample:
        if len(line) < LEAST:
            continue
        scores.append(surprise(line, counts, total))
        if len(scores) >= 4000:
            break
    if not scores:
        return None
    scores.sort()
    return scores[len(scores) // 2] * SLACK


def english_only(text, cut=None):
    """The lines of a text that read as English, joined. Nothing is read, only scored."""
    counts, total, _ = reference()
    if cut is None:
        cut = _default_cut()
    retained = []
    for line in text.splitlines():
        trimmed = line.strip()
        if len(trimmed) < LEAST:
            continue
        if not looks_like_writing(trimmed):
            continue
        if (cut is not None) and (surprise(trimmed, counts, total) > cut):
            continue
        retained.append(trimmed)
    return "\n".join(retained)


_cut = None


def _default_cut():
    """The cut, calibrated once against the reference corpus."""
    global _cut
    if _cut is None:
        counts, total, _ = reference()
        bulk = os.path.join(ROOT, "build", "corpora", "cc_english.txt")
        sample = []
        if os.path.isfile(bulk):
            with open(bulk, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    sample.append(line.strip())
                    if len(sample) >= 20000:
                        break
        _cut = cut_for(counts, total, sample)
    return _cut


def english_words(text):
    """Every ASCII word in a text that has an English shape, lowercased.

    Punctuation and glyphs never enter: WORD matches ASCII characters only. The
    orthography, the IPA, the Greek and the box drawing are gone before this reads a token.

    Two more types of residue survive that and are dropped here. A gloss label is written in ASCII
    capitals, COP and NMLZ and ERG and 3SBJ, and it passes every test for writing while being the
    grammar of another language. And a romanized Salishan form is ASCII characters with no vowel in
    it. English words carry a vowel; these do not.
    """
    held = []
    for one in WORD.findall(text):
        if one.isupper() and (len(one) <= 6):
            continue
        low = one.lower()
        if not VOWEL.search(low):
            continue
        held.append(low)
    return held


def main():
    out = io.TextIOWrapper(
        sys.stdout.buffer, encoding="utf-8", errors="replace", newline=""
    )
    counts, total, lines = reference()
    cut = _default_cut()
    out.write("\n  reference: %d byte pairs over %d lines\n" % (total, lines))
    out.write(
        "  cut: %.4f bits per pair, the reference's own median times %.2f\n"
        % (cut, SLACK)
    )

    papers = os.path.join(ROOT, "build", "papers")
    corpora = os.path.join(ROOT, "build", "corpora")
    targets = []
    if os.path.isdir(papers):
        held = []
        for name in sorted(os.listdir(papers)):
            if name.endswith(".txt"):
                with open(
                    os.path.join(papers, name), encoding="utf-8", errors="replace"
                ) as one:
                    held.append(one.read())
        targets.append(("research papers", "\n".join(held)))
    machine = os.path.join(corpora, "machine_prose.txt")
    if os.path.isfile(machine):
        with open(machine, encoding="utf-8", errors="replace") as one:
            targets.append(("fetched machine prose", one.read()))

    out.write(
        "\n  %-26s %12s %12s %12s %s\n"
        % ("corpus", "words in", "words retained", "ascii retained", "share")
    )
    for name, text in targets:
        before = len(text.split())
        gated = english_only(text, cut)
        after = len(gated.split())
        ascii_retained = len(english_words(gated))
        out.write(
            "  %-26s %12d %12d %12d %.2f\n"
            % (name, before, after, ascii_retained, ascii_retained / float(max(1, before)))
        )
    out.write("\n  no sample of any corpus is printed. These are counts.\n\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
