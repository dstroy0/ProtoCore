#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Date a body of prose by its word choice, against six decades of papers in the same register.
#
#   Usage:  python utils/maint/prose/prose_era.py [--markers N]
#
# THE THIRD STAGE OF THE SAME FILTER
#
# docs-check: quoting
# ban_evidence.py split the banned list into alphabet, word and phrase, and the three read different
# things off a text. The alphabet stage recovered the locale without being asked: neighbor fires at
# 17.3 per hundred thousand words in the papers and analyse at 8.9, because the proceedings are
# Canadian and British convention. This is the word stage, and the word stage carries the era.
# docs-check: end quoting
#
# The papers are dated. 81 of them open with a year from 1967 to 2013, and the ICSNL volumes carry
# their number, where volume n is the year 1965 + n. That is checked against two the extraction
# headers state outright: ICSNL 50 is 2015 and ICSNL 60 is 2025. Six decades, one register, one
# field, one conference. Vocabulary is the only thing free to move across them.
#
# ONLY ASCII WORDS ARE COUNTED
#
# These papers quote Salishan on every page. Counting every token would measure how much language
# data a paper prints, which changes with the decade for reasons that have nothing to do with how
# anybody writes. The count here is restricted to runs of ASCII characters, the English the
# linguist wrote around the data.
#
# WHAT IT CANNOT DO
#
# A decade holds a dozen papers and a handful of authors. An author and their decade are not
# separable here. A word can date a text or it can name the person who liked it. The marker list
# below is printed with the count of distinct papers each word appears in, and a marker sitting in
# one or two papers is one writer's habit wearing a decade's clothes.

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PAPERS = os.path.join(ROOT, "build", "papers")

sys.path.insert(0, HERE)

import prose_distance  # noqa: E402

WORD = re.compile(r"[a-z]{3,}")
LEADING_YEAR = re.compile(r"^(1[89]\d\d|20\d\d)")
VOLUME = re.compile(r"ICSNL[_ ]?(\d{2})")

# A decade needs this many words before it is used as a bin. Under it a rate rests on one paper.
LEAST = 20000

# Words too common to date anything, and the field's own subject matter, which is constant across
# every decade and would crowd the marker list with salish, suffix and vowel.
STOP = set("""
the and for that with this from are was were has have had not but its it is of to in on as at by an
be or which we they their there then than these those such other more max can may will would could
should one two three first second same other each any all some no nor if when where how why what who
whom whose does did done being been about into over under after before between during
salish salishan language languages suffix suffixes prefix root roots vowel vowels consonant
consonants stem stems form forms word words example examples section paper papers page pages
data speaker speakers dialect dialects verb verbs noun nouns phrase clause sentence
""".split())


def year_of(name):
    """The year a paper belongs to, from its filename.

    Two definitions. Max open with the year. The rest carry an ICSNL volume number, and volume n is
    the year 1965 + n, which the extraction headers confirm at both ends: volume 50 states 2015 and
    volume 60 states 2025.
    """
    found = LEADING_YEAR.match(name)
    if found:
        return int(found.group(1))
    found = VOLUME.search(name)
    if found:
        volume = int(found.group(1))
        if 40 <= volume <= 70:
            return 1965 + volume
    return None


def decade_of(year):
    return (year // 10) * 10


def words_of(text):
    """Every ASCII word of three characters or more, lowercased, with the stop list removed."""
    return [one for one in WORD.findall(text.lower()) if one not in STOP]


def profile(words):
    """A distribution over words, and the count it was taken over."""
    counts = {}
    for one in words:
        counts[one] = counts.get(one, 0) + 1
    total = len(words)
    if not total:
        return {}, 0
    return {k: c / total for k, c in counts.items()}, total


def distance(first, second):
    """Total variation between two word distributions, in [0, 1]."""
    run = 0.0
    for key in set(first) | set(second):
        run += abs(first.get(key, 0.0) - second.get(key, 0.0))
    return run / 2.0


def main():
    out = io.TextIOWrapper(
        sys.stdout.buffer, encoding="utf-8", errors="replace", newline=""
    )
    markers = 18
    if "--markers" in sys.argv:
        markers = int(sys.argv[sys.argv.index("--markers") + 1])

    if not os.path.isdir(PAPERS):
        out.write("  no build/papers to date against\n")
        out.flush()
        return 1

    bins = {}
    seen = {}
    undated = 0
    for name in sorted(os.listdir(PAPERS)):
        if not name.endswith(".txt"):
            continue
        year = year_of(name)
        if year is None:
            undated += 1
            continue
        with open(
            os.path.join(PAPERS, name), encoding="utf-8", errors="replace"
        ) as handle:
            text = handle.read()
        held = words_of(text)
        bins.setdefault(decade_of(year), []).extend(held)
        seen.setdefault(decade_of(year), []).append(name)
        for one in set(held):
            counted = seen.setdefault("papers_with", {})
            counted[one] = counted.get(one, 0) + 1

    spread = {
        one: bins[one]
        for one in bins
        if (isinstance(one, int) and len(bins[one]) >= LEAST)
    }
    if len(spread) < 3:
        out.write("  too few dated decades to build a timeline\n")
        out.flush()
        return 1

    out.write(
        "\n  the timeline, from %d dated papers, %d with no date in the name\n"
        % (sum(len(seen[one]) for one in spread), undated)
    )
    out.write("    %-8s %-8s %s\n" % ("decade", "papers", "english words"))
    profiles = {}
    for decade in sorted(spread):
        profiles[decade], total = profile(spread[decade])
        out.write("    %-8s %-8d %d\n" % ("%ds" % decade, len(seen[decade]), total))

    oldest, newest = min(profiles), max(profiles)

    out.write("\n  how far each decade sits from every other, in word choice\n")
    order = sorted(profiles)
    out.write(
        "    %-8s %s\n" % ("", "  ".join("%-6s" % ("%ds" % one) for one in order))
    )
    for row in order:
        cells = "  ".join(
            "%-6.3f" % distance(profiles[row], profiles[col]) for col in order
        )
        out.write("    %-8s %s\n" % ("%ds" % row, cells))
    out.write(
        "    a timeline shows as the numbers growing with the gap between two decades\n"
    )

    # Which words moved max between the two ends. A marker is a word that one end uses and the
    # other does not, and the paper count beside it says whether it is an era or one writer.
    appears = seen.get("papers_with", {})
    moved = []
    for word in set(profiles[oldest]) | set(profiles[newest]):
        early = profiles[oldest].get(word, 0.0)
        late = profiles[newest].get(word, 0.0)
        if appears.get(word, 0) < 4:
            continue
        moved.append((late - early, word, early, late, appears.get(word, 0)))
    moved.sort()

    out.write(
        "\n  words the %ds use and the %ds do not, per 10000 english words\n"
        % (oldest, newest)
    )
    out.write(
        "    %-18s %-9s %-9s %s\n"
        % ("word", "%ds" % oldest, "%ds" % newest, "in papers")
    )
    for _, word, early, late, papers in moved[:markers]:
        out.write(
            "    %-18s %-9.2f %-9.2f %d\n" % (word, early * 10000, late * 10000, papers)
        )

    out.write("\n  words the %ds use and the %ds did not\n" % (newest, oldest))
    out.write(
        "    %-18s %-9s %-9s %s\n"
        % ("word", "%ds" % oldest, "%ds" % newest, "in papers")
    )
    for _, word, early, late, papers in list(reversed(moved))[:markers]:
        out.write(
            "    %-18s %-9.2f %-9.2f %d\n" % (word, early * 10000, late * 10000, papers)
        )

    # Where this repository's own prose lands on that timeline.
    mine = []
    for path in prose_distance.repository_files():
        mine.append(prose_distance.prose_of(path))
    ours, total = profile(words_of(" ".join(mine)))
    out.write("\n  this repository, %d english words, against each decade\n" % total)
    scored = sorted((distance(ours, profiles[one]), one) for one in profiles)
    for score, decade in scored:
        out.write(
            "    %-8s %.4f%s\n"
            % ("%ds" % decade, score, "   nearest" if decade == scored[0][1] else "")
        )

    # A decade against its own neighbors, for scale. A repository distance has to be read against
    # how far two decades of the same field already sit from each other.
    steps = [
        distance(profiles[order[at]], profiles[order[at + 1]])
        for at in range(len(order) - 1)
    ]
    if steps:
        out.write(
            "\n  one decade to the next averages %.4f. A distance near that is one\n"
            % (sum(steps) / len(steps))
        )
        out.write(
            "  decade's worth of drift, and the repository sits %.4f from its nearest\n"
            % scored[0][0]
        )

    out.write("\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
