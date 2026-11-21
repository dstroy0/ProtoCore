#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Does this prose sound like a person wrote it? Answered as a rate against a drawn bar.
#
#   Usage:  python utils/maint/prose/docs_check --rate PATH [PATH ...]
#
# docs_check answers a different question. It reports every banned token it finds. A file with
# no findings passes, and passing a ban list is not the same as reading human: a writer who avoids
# eight named phrases and retains the rhythm that produced them still reads wrong, and the ban list
# only ever names the shapes somebody already noticed.
#
# This counts the same patterns as a RATE per hundred thousand words and puts it beside the three
# rates already measured in this tree, quoted in docs_check's own header:
#
#     643.4   machine prose, 38,702 gated words of session transcript
#     387.9   human prose, 759,815 words of the research papers
#     112.8   this tree, after a day of repair
#
# Those are the bar, and they were drawn and not derived, the rule this tree applies to
# every other threshold. A file scoring near 643 reads like a machine however clean its findings
# list is. A file near 112 reads like this tree.
#
# THE PATTERNS ARE IMPORTED AND NEVER COPIED. A second ban list is the same defect one level up, and
# check_fixes.py makes the same argument about the rewrite table.
#
# WHAT THIS DOES NOT REPLACE
#
# orior/maint/prose/machine_distance.py is the better instrument for the register question and
# it existed before this file did. It has two poles, a human corpus and a page written deliberately
# in the machine register, and it places a file by which it sits nearer, with the margin reported
# against a band measured at that file's own word count. That is a positive control, and this file
# has none: a rate against three quoted numbers cannot say what a file resembles, only how often it
# uses named phrases.
#
# Run machine_distance.py first. It states it does not measure arrangement, since
# its file-level distances are taken on a bag of words and it says so at the foot of its own output.
# The RHYTHM section below is that missing half and is the only reason to run this.

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

from .bans import BANNED
from . import human_rate

MACHINE_RATE = 643.4
HUMAN_RATE = 387.9
TREE_RATE = 112.8

READ = (".md", ".tex")


def words_and_hits(text):
    """Counts English words, and every banned pattern firing in them."""
    # Strip what is not running prose. A table of figures does not dilute the rate.
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    text = re.sub(r"\\begin\{(tabular|verbatim|center)\}.*?\\end\{\1\}", " ", text, flags=re.S)
    text = re.sub(r"`[^`]*`", " ", text)
    text = re.sub(r"\\texttt\{[^}]*\}", " ", text)
    text = re.sub(r"\\\(.*?\\\)", " ", text, flags=re.S)
    text = re.sub(r"^\s*[%#].*$", " ", text, flags=re.M)
    text = re.sub(r"\\[a-zA-Z]+", " ", text)

    words = len(re.findall(r"[A-Za-z][A-Za-z'-]*", text))

    hits = []
    for pattern in BANNED:
        found = re.findall(pattern, text, flags=re.I)
        if found:
            hits.append((pattern, len(found)))
    return words, hits


def sentences(text):
    """Sentence lengths in words, for the rhythm measurement.

    The ban rate above counts vocabulary. It cannot answer the question it is usually asked, because
    a writer avoiding eight named phrases while retention the cadence that produced them scores zero
    and still reads wrong. That is this tree's own mode 15 one level up: a reading whose dimension is
    too small to carry the effect being claimed.

    Rhythm is the part that survives a vocabulary sweep. Human prose is bursty, mixing four-word
    sentences with forty-word ones; generated prose runs closer to its own mean. The statistic is the
    coefficient of variation of sentence length, which is unitless and therefore comparable between
    a chapter and a paper.
    """
    text = re.sub(r"\s+", " ", text)
    parts = re.split(r"(?<=[.!?])\s+(?=[A-Z(])", text)
    return [len(re.findall(r"[A-Za-z][A-Za-z'-]*", p)) for p in parts
            if len(re.findall(r"[A-Za-z][A-Za-z'-]*", p)) >= 3]


def burstiness(lengths):
    """Mean, spread and coefficient of variation of sentence length."""
    if len(lengths) < 5:
        return 0.0, 0.0, 0.0
    mean = sum(lengths) / float(len(lengths))
    spread = (sum((n - mean) ** 2 for n in lengths) / float(len(lengths))) ** 0.5
    return mean, spread, (spread / mean if mean else 0.0)


def verdict(rate):
    """Which of the three drawn rates this sits nearest."""
    if rate <= (TREE_RATE + HUMAN_RATE) / 2:
        return "reads like this tree"
    if rate <= (HUMAN_RATE + MACHINE_RATE) / 2:
        return "reads human"
    return "READS LIKE A MACHINE"


def collect(paths):
    found = []
    for path in paths:
        if os.path.isfile(path):
            found.append(path)
            continue
        for dirpath, dirnames, filenames in os.walk(path):
            dirnames[:] = [d for d in dirnames if d not in {".git", "build", "__pycache__"}]
            found += [os.path.join(dirpath, f) for f in filenames if f.endswith(READ)]
    return sorted(found)


def show_rate(roots):
    paths = collect(roots or ["docs", "theory"])
    if not paths:
        print("  no files were read. Nothing was checked, so nothing passed.")
        return 2

    print("  drawn bars, from docs_check: machine %.1f   human %.1f   this tree %.1f"
          % (MACHINE_RATE, HUMAN_RATE, TREE_RATE))
    print("  %-58s %7s %8s  %s" % ("file", "words", "per100k", "verdict"))

    total_words = 0
    total_hits = 0
    worst = []

    rhythm = []

    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as handle:
            raw = handle.read()
        words, hits = words_and_hits(raw)
        if words < 200:
            continue
        count = sum(n for _, n in hits)
        rate = (count * 100000.0) / words
        total_words += words
        total_hits += count
        worst.append((rate, path, words, hits))
        rhythm.append((path, burstiness(sentences(re.sub(r"\\[a-zA-Z]+\{?", " ", raw)))))

    for rate, path, words, _ in sorted(worst, reverse=True):
        print("  %-58s %7d %8.1f  %s" % (path.replace("\\", "/")[-58:], words, rate, verdict(rate)))

    print("")
    print("  RHYTHM. Sentence length, and how much it varies. Vocabulary sweeps do not touch this.")
    print("  %-58s %6s %7s %7s" % ("file", "mean", "spread", "cv"))
    for path, (mean, spread, cv) in sorted(rhythm, key=lambda r: r[1][2]):
        if mean:
            print("  %-58s %6.1f %7.1f %7.3f" % (path.replace("\\", "/")[-58:], mean, spread, cv))

    usable = [cv for _, (mean, _, cv) in rhythm if mean]
    if usable:
        print("")
        print("  coefficient of variation, low to high: %.3f to %.3f, median %.3f"
              % (min(usable), max(usable), sorted(usable)[len(usable) // 2]))
        print("  A file well below the others is writing at one length, which is the tell a ban")
        print("  list cannot see. Compare a suspect file against this tree's own chapters, since")
        print("  those are the drawn bar for what this corpus sounds like.")

    if total_words:
        overall = (total_hits * 100000.0) / total_words
        print("")
        print("  %d words, %d hits, %.1f per 100k overall: %s"
              % (total_words, total_hits, overall, verdict(overall)))

        # The shapes carrying the rate, since a number without its cause cannot be acted on.
        tally = {}
        for _, _, _, hits in worst:
            for pattern, n in hits:
                tally[pattern] = tally.get(pattern, 0) + n
        if tally:
            print("  the shapes carrying it:")
            for pattern, n in sorted(tally.items(), key=lambda kv: -kv[1])[:8]:
                human = human_rate.HUMAN_RATE.get(pattern, 0.0)
                mine = (n * 100000.0) / total_words
                times = ("%.0fx human" % (mine / human)) if human else "absent from human prose"
                print("    %-52s %3d  %6.1f  %s" % (pattern[:52], n, mine, times))

    return 0
