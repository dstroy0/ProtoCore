#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Find the machine register in this tree by a measurement against a written sample of it.
#
#   Usage:  python utils/maint/prose/machine_distance.py [<root> ...] [--worst N]
#
# WHY A POSITIVE CONTROL CHANGES THE QUESTION
#
# The other instruments here have one pole. The english_sift tool scores a text against ordinary English, and
# ban_evidence scores a phrase against 154 human papers. The prose_era tool dates a text against six decades of them.
# All three ask how far a text sits from something human. None of them can say what it sits near instead. A detector
# with one pole can only ever report a distance from normal.
#
# utils/maint/prose/fixtures/machine_reference.md is the other pole. It is a page in the machine register, at full
# strength, by the machine, about the work in this repository. The match of subject makes it usable, because it is not a
# distance to a 1667 epic or to a linguistics paper. Genre, locale and era all stay fixed, and the only thing left free
# to vary is the register.
#
# THE SPLIT boundary_check ALREADY USES
#
# Two poles, and every file joins whichever it is nearer. There is no threshold in that, and no label that anybody
# supplied. A file's verdict is which of the two references it resembles more, and the margin between the two distances
# says how firmly. The papers are the human pole.
#
# WHAT WOULD MAKE THIS WRONG
#
# The positive pole is one page by one writer on one day. It is a sample of the register, and not the register. A file
# can sit near it for a shared subject, and not a shared voice. The margin column is there for that. This does not place
# a file inside the resolution reported at the foot, and the honest answer for it is that the instrument read nothing.
#
# The fixture must never get a repair. The docs_check tool skips the directory that holds it for that reason, and a
# repair would delete the only positive sample that the instrument has.

import io
import math
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
WORDS = os.path.join(HERE, "voice.tsv")
WORD_WEB = os.path.join(HERE, "voice_word_web.tsv")
FIXTURE = os.path.join(HERE, "fixtures", "machine_reference.md")

sys.path.insert(0, HERE)

import prose_distance  # noqa: E402
from english_gate import english_only, english_words  # noqa: E402

# The poles, best provenance first.
#
# session_prose.txt is the machine's own messages, taken out of a transcript by session_prose.py. Nobody needs to trust
# a label for it: the turns come from the model itself. It is also the largest machine sample here.
SESSION = os.path.join(ROOT, "build", "corpora", "session_prose.txt")

# The published fallback, whose label nobody outside can verify.
FETCHED = os.path.join(ROOT, "build", "corpora", "machine_prose.txt")

# A file carries this much prose before the instrument places it. Under it, one sentence decides the verdict.
LEAST = 500

# Words that name what this repository is about. They are the subject, and both poles discuss it. With them in, a file
# would join whichever pole happened to mention anchors more often.
# docs-check: quoting
SUBJECT = set("""
anchor anchors sift sifting corpus corpora prose text texts word words phrase phrases file files
repository tree code line lines comment comments document documents page pages check checker
paper papers human english language measure measured measurement measurements distance pattern
patterns register banned ban list stage stages read reads reading write writes written writing
salish salishan entropy byte bytes pair pairs symbol symbols
lushootseed halkomelem sliammon sechelt squamish klallam twana tillamook quinault chehalis
cowlitz nlekepmxcin thompson okanagan nsyilxcn statimcets comox saanich songish samish musqueam
cowichan chilliwack bella coola nater lyon hilbert davis mellesmoen kye wolfe robertson garcia
icsnl ubcwpl proceedings conference gloss glosses glossing morpheme morphemes affix affixes
""".split())
# docs-check: end quoting

# The medium, which is not the register.
#
# The machine pole is a chat transcript, and the files under test are documents. A transcript says you and your and I'll
# and that's, because it speaks to somebody, and a README never does. Left in, those words dominate the comparison, and
# it measures which medium a text is in. The explain mode shows it plainly. You, it's, that's, your, me and i'll are the
# strongest single pulls away from the machine pole, in a file that could not use them.
# docs-check: quoting
MEDIUM = set("""
i i'm i'll i've me my mine myself we we're we'll we've our ours us
you you're you'll you've your yours yourself
it's that's there's here's isn't aren't don't doesn't didn't can't won't wouldn't couldn't
let's ok okay yes no yeah sure thanks please sorry hi hello
""".split())
# docs-check: end quoting

# The two groups that --decompose sorts a margin into.
#
# SHAPE is the parataxis signature: short present tense declaratives joined with and. A file in that style runs high on
# is and and, and low on of, in, as and be. Those are the words that subordination needs, and this house style does not
# subordinate. Section 6 of the documentation standard requires exactly that. A margin that this group carries is the
# standard at work, and not a defect. Both worst files put 42 percent of their margin here.
SHAPE = set("is and of in are as be to the a that it for this with by on".split())

# ABSOLUTE is what the standard already governs under No Uncited Absolutes. It carries 4.1 percent
# of the ledger's margin and 3.6 percent of the C README's. A pass that cut 33 unearned absolutes
# out of the ledger moved it 0.0021. That group had no more than 0.0064 to give.
ABSOLUTE = set("every no nothing only never always all none any cannot not".split())


def words_of(text):
    """The English words of a text, with the shared subject removed.

    Both stages of english_gate run first. Without them the human pole is 31 percent non-English. Every comparison
    against it then reads how much Salishan a text prints, before it reads a word of anybody's register.
    """
    return [one for one in english_words(english_only(text)) if (one not in SUBJECT) and (one not in MEDIUM)]


# How many words of each pole the comparison runs over.
#
# A comparison of full word distributions places nothing. The positive pole is one page of about 1200 words, and the
# human pole is 982,000. A total variation over every word that either of them uses is then almost all sampling noise.
# The own halves of the machine pole sit far further apart than the largest margin that any file reaches, and nothing
# can clear that.
#
# A limit to the commonest words of each pole is the same move that web() makes with its 64 ranks. A register lives in
# the words that it reaches for often, and the tail is where the sampling noise is.
RANKS = 150


def top_words(profiles, ranks=RANKS):
    """The union of the commonest words of each profile, as the axis to compare on."""
    held = set()
    for one in profiles:
        held.update(sorted(one, key=one.__getitem__, reverse=True)[:ranks])
    return held


def restricted(profile_of, vocabulary):
    """One profile cut to a fixed vocabulary and renormalized over it."""
    retained = {one: profile_of.get(one, 0.0) for one in vocabulary}
    total = sum(retained.values())
    if total <= 0.0:
        return {}
    return {one: value / total for one, value in retained.items()}


def profile(words):
    counts = {}
    for one in words:
        counts[one] = counts.get(one, 0) + 1
    total = len(words)
    if not total:
        return {}, 0
    return {k: c / total for k, c in counts.items()}, total


def counted_table(path, keys):
    """A distribution read from a counted table: the first `keys` columns name it, and the next counts it.

    The tools count the corpus once. voice_count.py writes the words, and voice_web.py writes the pairs.
    """
    counts = {}
    total = 0
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                parts = line.rstrip("\r\n").split("\t")
                if len(parts) < keys + 1 or not parts[keys].isdigit():
                    continue
                key = " ".join(parts[:keys])
                counts[key] = counts.get(key, 0) + int(parts[keys])
                total += int(parts[keys])
    except OSError:
        return {}, 0
    if not total:
        return {}, 0
    return {k: c / total for k, c in counts.items()}, total


def distance(first, second):
    """Total variation between two word distributions, in [0, 1]."""
    run = 0.0
    for key in set(first) | set(second):
        run += abs(first.get(key, 0.0) - second.get(key, 0.0))
    return run / 2.0


def web_profile(words, run=2):
    """A distribution over runs of `run` adjacent words: the word web instead of the bag of words.

    A bag of words does not change under permutation: shuffle the corpus, and every count stays identical. The null
    permutation against a bag is therefore exactly zero. The ledger of this repository already records the general form
    of that. Collision entropy carries the same invariance, and cannot separate a corpus from its own shuffle.

    Register is a fact about arrangement. A habit of phrase is which word comes after which, and an adjacent pair is the
    smallest reading that a shuffle destroys.
    """
    counts = {}
    total = 0
    for at in range(len(words) - run + 1):
        key = " ".join(words[at : at + run])
        counts[key] = counts.get(key, 0) + 1
        total += 1
    if not total:
        return {}, 0
    return {k: c / total for k, c in counts.items()}, total


def shuffled(words, seed):
    """The same words in another order. The max entropy arrangement of one multiset."""
    held = list(words)
    random.Random(seed).shuffle(held)
    return held


def prose_of_path(path, where=None):
    """The prose of one file, for the explain mode.

    `where` is the tree that the path is relative to, and it defaults to this repository. It is a parameter because the
    caller may measure another repository.
    """
    return prose_distance.prose_of(os.path.join(where or prose_distance.ROOT, path))


def pulls(mine, machine, human, ranks=24):
    """Which words carry one file toward the machine pole, and which carry it away.

    A total variation distance is a sum over words, and it comes apart again into the words that made it. For one word,
    the contribution to the margin is the distance of the file from the human rate, less its distance from the machine
    rate. Positive means that the word pulls the file toward the machine pole.

    A phrase list cannot do this part. It names the words that do the work in this file, the ones that nobody wrote down
    among them, in the order that they matter.
    """
    scored = []
    for word in set(mine) | set(machine) | set(human):
        here = mine.get(word, 0.0)
        there = machine.get(word, 0.0)
        theirs = human.get(word, 0.0)
        scored.append((abs(here - theirs) - abs(here - there), word, here, there, theirs))
    scored.sort(reverse=True)
    return scored[:ranks], scored[-ranks:]


def halves(text):
    """A text split by alternate sentences, for a resolution floor.

    Alternate sentences, and not a cut at the midpoint. The ledger records that a midpoint cut lands on a document
    boundary. It then reports the distance between whichever documents it separated, and that inflates every floor by
    1.35 to 3.64 times.
    """
    parts = [one for one in re.split(r"(?<=[.!?])\s+", text) if one.strip()]
    if len(parts) < 8:
        return None, None
    return " ".join(parts[0::2]), " ".join(parts[1::2])


# What margin a text of this length reaches when a human wrote it.
#
# One floor for all lengths does not work. A floor from a split of the whole paper set in half does not measure the same
# thing as a short README. A profile from a few hundred words is sparse, and sits far from both poles. The difference of
# two large distances is not the difference of two small ones. One number would be too strict on a short file, and far
# too lenient on a long one.
#
# The table is a measurement. Half the papers stay out as the reference pole, and the other half go into disjoint
# blocks. The code scores each block the way it scores a repository file. Every block is human, and the spread is what
# the instrument does to a text of that length. Reference pole 372,922 words, probe pool 366,833 words, machine pole
# 35,303 words, the two poles 0.3356 apart.
#
#   words  blocks   median      p95      p99    worst
#     250    1467  -0.1000  -0.0199   0.0099   0.0356
#     500     733  -0.1257  -0.0332   0.0030   0.0299
#    1000     366  -0.1521  -0.0422  -0.0054   0.0081
#    2000     183  -0.1756  -0.0693  -0.0020   0.0091
#    4000      91  -0.1957  -0.1066  -0.0256  -0.0056
#    8000      45  -0.2149  -0.1642  -0.0746  -0.0203
#   16000      22  -0.2384  -0.2051  -0.1120  -0.0873
#   22000      16  -0.2446  -0.1942  -0.1468  -0.1349
#   42000       8  -0.2585  -0.2445  -0.2443  -0.2442
#
# The median falls as the block grows. A longer sample resolves both poles better, and the gap between two
# better-resolved distances widens. A file goes against the worst column: the highest margin that any human block of
# that length reached. A file above it is outside everything that 366,833 words of human writing did at its own size.
# These numbers belong to the two poles that produced them. Run --band to measure them again when either pole changes.
HUMAN_BAND = (
    (250, -0.1000, 0.0356),
    (500, -0.1257, 0.0299),
    (1000, -0.1521, 0.0081),
    (2000, -0.1756, 0.0091),
    (4000, -0.1957, -0.0056),
    (8000, -0.2149, -0.0203),
    (16000, -0.2384, -0.0873),
    (22000, -0.2446, -0.1349),
    (42000, -0.2585, -0.2442),
)


def band_at(count):
    """The median and the highest margin that a human text of this many words reached.

    The code interpolates on the logarithm of the word count. Each row of the table has about twice the words of the row
    before. The sampling error moves with the square root of the sample. Outside the measured range, the code returns
    the end row unchanged. The 250 word row judges a 200 word file, and the 42,000 word row a 200,000 word file.
    """
    if count <= HUMAN_BAND[0][0]:
        return HUMAN_BAND[0][1], HUMAN_BAND[0][2]
    if count >= HUMAN_BAND[-1][0]:
        return HUMAN_BAND[-1][1], HUMAN_BAND[-1][2]
    for index in range(1, len(HUMAN_BAND)):
        size, median, worst = HUMAN_BAND[index]
        if count <= size:
            previous_size, previous_median, previous_worst = HUMAN_BAND[index - 1]
            span = math.log(size) - math.log(previous_size)
            part = (math.log(count) - math.log(previous_size)) / span
            return (
                previous_median + (median - previous_median) * part,
                previous_worst + (worst - previous_worst) * part,
            )
    return HUMAN_BAND[-1][1], HUMAN_BAND[-1][2]


def measure_band(out, machine_text, human_text):
    """Measure HUMAN_BAND again, for when either pole changes.

    The blocks must exclude the reference pole. Sentences alternate into a reference half and a probe half. The code
    cuts the probe half into disjoint blocks, and scores each block the way it scores a repository file. Alternate
    sentences, and not a cut at the midpoint, for the reason on halves. A midpoint cut lands on a paper boundary, and
    reports the distance between whichever papers it separated.
    """
    reference_text, probe_text = halves(human_text)
    if not reference_text:
        out.write("  not enough human text to measure a band\n")
        return 1

    machine_full, machine_words = profile(words_of(machine_text))
    reference_full, reference_words = profile(words_of(reference_text))
    probe_all = words_of(probe_text)

    vocabulary = top_words((machine_full, reference_full))
    machine = restricted(machine_full, vocabulary)
    reference = restricted(reference_full, vocabulary)

    out.write("\n  reference pole  %7d words, half the papers\n" % reference_words)
    out.write("  probe pool      %7d words, the other half\n" % len(probe_all))
    out.write("  machine pole   %7d words\n" % machine_words)
    out.write("  the two poles sit %.4f apart\n" % distance(machine, reference))
    out.write("\n  margin of a known human block, by block size\n")
    out.write("  %7s %7s %9s %9s %9s\n" % ("words", "blocks", "median", "p95", "worst"))

    for size, _, _ in HUMAN_BAND:
        margins = []
        for start in range(0, len(probe_all) - size + 1, size):
            block_full, _ = profile(probe_all[start : start + size])
            block = restricted(block_full, vocabulary)
            margins.append(distance(block, reference) - distance(block, machine))
        if not margins:
            continue
        margins.sort()
        place = 0.95 * (len(margins) - 1)
        low = int(place)
        high = min(low + 1, len(margins) - 1)
        out.write(
            "  %7d %7d %9.4f %9.4f %9.4f\n"
            % (
                size,
                len(margins),
                margins[len(margins) // 2],
                margins[low] + (margins[high] - margins[low]) * (place - low),
                margins[-1],
            )
        )
    out.write("\n  copy the words, median and worst columns into HUMAN_BAND above.\n\n")
    return 0


def main():
    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="")
    worst = 30
    if "--worst" in sys.argv:
        worst = int(sys.argv[sys.argv.index("--worst") + 1])

    # The tree under measurement, which is not always this one. A path accepted and then ignored would give a caller
    # that measures another repository the number of this one, with no sign of it. The output echoes the path for the
    # same reason as the file count. A number with no subject named beside it is a number that somebody will attach to
    # the wrong thing.
    where, roots = prose_distance.named_roots(sys.argv)
    if where is not None:
        out.write("\n  measuring %s\n" % where.replace("\\", "/"))

    # The fetched million words are the pole. The hand-written fixture stays as a check on it, because it is the
    # register at full strength by design. It should land near the fetched pole. If it does not, one of the two is not
    # what it claims.
    fixture_text = ""
    if os.path.isfile(FIXTURE):
        with open(FIXTURE, encoding="utf-8", errors="replace") as handle:
            fixture_text = handle.read()

    if os.path.isfile(SESSION):
        with open(SESSION, encoding="utf-8", errors="replace") as handle:
            machine_text = handle.read()
        pole_name = os.path.relpath(SESSION, ROOT).replace("\\", "/") + "  (own transcript)"
    elif os.path.isfile(FETCHED):
        with open(FETCHED, encoding="utf-8", errors="replace") as handle:
            machine_text = handle.read()
        pole_name = os.path.relpath(FETCHED, ROOT).replace("\\", "/") + "  (label unverified)"
    elif fixture_text:
        machine_text = fixture_text
        pole_name = os.path.relpath(FIXTURE, ROOT).replace("\\", "/") + "  (hand written, small)"
    else:
        out.write("  no machine pole. Run utils/maint/data/fetch/fetch_machine_prose.py\n")
        out.flush()
        return 1

    human_full, human_words = counted_table(WORDS, 1)
    if not human_full:
        out.write("  no human pole in %s\n" % os.path.relpath(WORDS, ROOT))
        out.flush()
        return 1
    # the instrument does not read the papers themselves. A pole is every word and how often it occurs, and voice.tsv is
    # that, counted once by voice_count.py over the corpus
    human_text = ""

    machine_all = words_of(machine_text)
    machine_full, machine_words = profile(machine_all)

    vocabulary = top_words((machine_full, human_full))
    machine = restricted(machine_full, vocabulary)
    human = restricted(human_full, vocabulary)

    out.write("\n  the two poles\n")
    out.write("    %-10s %7d words  %s\n" % ("machine", machine_words, pole_name))
    out.write("    %-10s %7d words  utils/maint/prose/voice.tsv\n" % ("human", human_words))
    out.write("    compared over the %d commonest words of the two\n" % len(vocabulary))
    out.write("    they sit %.4f apart\n" % distance(machine, human))

    # The floor. The code splits each pole by alternate sentences, and measures its two halves against each other. The
    # instrument does not place a file whose margin is under this. The floor must sit on the same axis as the margin:
    # the resolution of the measurement actually made. A floor over every word that either pole uses belongs to a
    # different instrument than a margin over the rank-limited vocabulary.
    floors = []
    for name, text in (("machine", machine_text), ("human", human_text[:400000])):
        first, second = halves(text)
        if not first:
            continue
        one, _ = profile(words_of(first))
        two, _ = profile(words_of(second))
        floors.append((name, distance(restricted(one, vocabulary), restricted(two, vocabulary))))
    out.write("\n  resolution, each pole against its own alternating halves\n")
    for name, value in floors:
        out.write("    %-10s %.4f\n" % (name, value))
    out.write("    a pole against itself, which bounds nothing about a 500 word file. The band\n")
    out.write("    above HUMAN_BAND is what places a file, and it is measured at that file's size.\n")

    if "--band" in sys.argv:
        code = measure_band(out, machine_text, human_text)
        out.flush()
        return code

    rows = []
    for path in prose_distance.repository_files(where, roots):
        if "fixtures" in path:
            continue
        text = prose_distance.prose_of(os.path.join(where or prose_distance.ROOT, path))
        if len(text) < LEAST:
            continue
        mine_full, count = profile(words_of(text))
        if not mine_full:
            continue
        mine = restricted(mine_full, vocabulary)
        if not mine:
            continue
        to_machine = distance(mine, machine)
        to_human = distance(mine, human)
        margin = to_human - to_machine
        human_median, human_worst = band_at(count)
        rows.append((margin - human_worst, margin, human_median, count, path))
    rows.sort(reverse=True)

    if not rows:
        out.write("  no repository prose to place\n")
        out.flush()
        return 1

    outside = [one for one in rows if one[0] > 0]
    out.write("\n  %d files placed against the human band at their own word count\n" % len(rows))
    out.write("  %d sit above everything %d words of human writing did at their size\n" % (len(outside), 366833))

    out.write("\n  furthest outside the human band, worst first\n")
    out.write("    %-8s %-8s %-8s %-7s %s\n" % ("excess", "margin", "human", "words", "file"))
    for excess, margin, median, count, path in rows[:worst]:
        out.write("    %+-8.4f %+-8.4f %+-8.4f %-7d %s\n" % (excess, margin, median, count, path))
    out.write("  excess is the margin less the highest any human block of that length reached.\n")
    out.write("  human is where the median human block of that length sits.\n")

    # One file explained. Which words in it carry it toward the machine pole, named in order.
    if "--explain" in sys.argv:
        wanted = sys.argv[sys.argv.index("--explain") + 1].replace("\\", "/")
        text = prose_of_path(wanted, where)
        if not text:
            out.write("\n  no prose at %s\n" % wanted)
        else:
            mine_full, count = profile(words_of(text))
            mine = restricted(mine_full, vocabulary)
            toward, away = pulls(mine, machine, human)
            out.write("\n  %s, %d words\n" % (wanted, count))
            out.write("  what pulls it toward the machine pole, per thousand words in each\n")
            out.write("    %-18s %8s %8s %8s\n" % ("word", "file", "machine", "human"))
            for value, word, here, there, theirs in toward:
                if value <= 0:
                    continue
                out.write("    %-18s %8.2f %8.2f %8.2f\n" % (word, here * 1000, there * 1000, theirs * 1000))
            out.write("  and what pulls it the other way\n")
            for value, word, here, there, theirs in reversed(away):
                if value >= 0:
                    continue
                out.write("    %-18s %8.2f %8.2f %8.2f\n" % (word, here * 1000, there * 1000, theirs * 1000))
        out.write("\n")
        out.flush()
        return 0

    # The margin of one file, split into the words that make it, with no remainder.
    #
    # The margin is a difference of two total variation distances, and total variation is a sum over words. The margin
    # is therefore a sum over words, and every term is the share of one word in the verdict:
    #
    #     margin = sum over w of 0.5 * ( |file(w) - human(w)| - |file(w) - machine(w)| )
    #
    # An editor needs this to know whether a repair can work on a file. The explain mode above names the words, and this
    # says how much of the reading each is worth. An edit pass can move the margin by no more than the share of the
    # words that it touches.
    if "--decompose" in sys.argv:
        wanted = sys.argv[sys.argv.index("--decompose") + 1].replace("\\", "/")
        text = prose_of_path(wanted, where)
        if not text:
            out.write("\n  no prose at %s\n" % wanted)
            out.flush()
            return 1
        mine_full, count = profile(words_of(text))
        mine = restricted(mine_full, vocabulary)
        terms = []
        for word in vocabulary:
            here = mine.get(word, 0.0)
            theirs = human.get(word, 0.0)
            ours = machine.get(word, 0.0)
            terms.append(
                (
                    0.5 * (abs(here - theirs) - abs(here - ours)),
                    word,
                    here,
                    ours,
                    theirs,
                )
            )
        terms.sort(reverse=True)
        total = sum(one[0] for one in terms)

        # Shares are a fraction of the margin, and a file near zero has almost no margin to take a fraction of. A margin
        # that small gives a cumulative share of many hundred percent, and that is arithmetic and not a reading. Below a
        # tenth of the smallest human band row, the shares come out, and the contributions stand on their own.
        readable = abs(total) >= 0.01

        out.write("\n  %s, %d words\n" % (wanted, count))
        out.write("  margin %+.4f, summed over %d words with no remainder\n" % (total, len(vocabulary)))
        if not readable:
            out.write("  too near zero for a share to mean anything. Contributions only.\n")
        out.write("\n  %-14s %9s %8s %9s %9s %9s\n" % ("word", "carries", "cumul", "file/1k", "machine/1k", "human/1k"))
        running = 0.0
        for value, word, here, ours, theirs in terms[:22]:
            running += value
            if readable:
                measure = "%8.1f%% %7.1f%%" % (
                    100.0 * value / total,
                    100.0 * running / total,
                )
            else:
                measure = "%+9.4f %+8.4f" % (value, running)
            out.write("  %-14s %s %9.2f %9.2f %9.2f\n" % (word, measure, here * 1000, ours * 1000, theirs * 1000))

        shape = sum(one[0] for one in terms if one[1] in SHAPE)
        absolute = sum(one[0] for one in terms if one[1] in ABSOLUTE)
        out.write("\n  %-42s %9s %8s\n" % ("group", "margin", "share"))
        for name, value in (
            ("sentence shape", shape),
            ("absolutes", absolute),
            ("everything else", total - shape - absolute),
        ):
            share = "%7.1f%%" % (100.0 * value / total) if readable else "      --"
            out.write("  %-42s %+9.4f %s\n" % (name, value, share))
        out.write("\n  the shape group is what section 6 of the documentation standard requires:\n")
        out.write("  one fact per sentence, in plain declarative order. Moving it toward the\n")
        out.write("  papers means writing longer subordinated sentences, which is a worse\n")
        out.write("  document and a better score. Do not trade the first for the second.\n\n")
        out.flush()
        return 0

    out.write("\n  deepest inside the human band, for contrast\n")
    for excess, margin, median, count, path in rows[-5:]:
        out.write("    %+-8.4f %+-8.4f %+-8.4f %-7d %s\n" % (excess, margin, median, count, path))

    out.write("\n  margin is human distance minus machine distance. Positive means the file reads\n")
    out.write("  more like the machine sample than like the papers.\n")

    # The null permutation, run against each pole itself. This is the check that says whether the
    # measure above can see anything at all.
    out.write("\n  the null permutation, each pole against a shuffle of its own words\n")
    out.write("    %-12s %-16s %s\n" % ("pole", "bag of words", "word web"))
    webs = {}
    for name, words in (("machine", machine_all),):
        turned = shuffled(words, 0x5EED)
        bag_real, _ = profile(words)
        bag_null, _ = profile(turned)
        web_real, _ = web_profile(words)
        web_null, _ = web_profile(turned)
        webs[name] = web_real
        out.write("    %-12s %-16.4f %.4f\n" % (name, distance(bag_real, bag_null), distance(web_real, web_null)))
    # the human pole's own pairs, counted once over the corpus by voice_web.py. The pairs of a
    # shuffle are pairs drawn from the bag. The null is therefore the product of the two word counts
    webs["human"], _ = counted_table(WORD_WEB, 2)
    if webs["human"]:
        web_null = {}
        for key in webs["human"]:
            first, _, second = key.partition(" ")
            web_null[key] = human_full.get(first, 0.0) * human_full.get(second, 0.0)
        over = sum(web_null.values())
        web_null = {k: v / over for k, v in web_null.items()} if over else web_null
        out.write("    %-12s %-16.4f %.4f\n" % ("human", 0.0, distance(webs["human"], web_null)))
    out.write("    a bag of words returns exactly zero, because a shuffle does not change one.\n")
    out.write("    every distance printed above this line was taken on that bag. Every one of\n")
    out.write("    them reads composition instead of arrangement.\n")

    if ("machine" in webs) and ("human" in webs):
        out.write("\n  the two poles as word webs sit %.4f apart\n" % distance(webs["machine"], webs["human"]))

    # The whole tree as one web. A single file holds a few hundred words, and its web is nearly all noise. For that
    # reason, the output reports no web distance per file. The repository as one body is 400,000 words, and that reading
    # is one that a web can give.
    whole = []
    for path in prose_distance.repository_files(where, roots):
        if "fixtures" in path:
            continue
        whole.extend(words_of(prose_distance.prose_of(os.path.join(where or prose_distance.ROOT, path))))
    if whole and ("machine" in webs):
        ours, count = web_profile(whole)
        null, _ = web_profile(shuffled(whole, 0x5EED))
        own = distance(ours, null)
        to_machine = distance(ours, webs["machine"])
        to_human = distance(ours, webs["human"])
        out.write("\n  the whole tree as one web, %d words\n" % len(whole))
        out.write("    against its own shuffle   %.4f\n" % own)
        out.write("    to the machine pole     %.4f\n" % to_machine)
        out.write("    to the human pole         %.4f\n" % to_human)
        out.write(
            "    margin %+.4f toward %s\n"
            % (
                to_human - to_machine,
                "the machine" if to_machine < to_human else "the humans",
            )
        )
        nearer = [one for one in (to_machine, to_human) if one < own]
        out.write("    a web distance is only worth reading against how far a shuffle already\n")
        out.write("    sits, and that is %.4f here.\n" % own)
        if not nearer:
            out.write("    both poles sit further than the shuffle. Neither is resolved. At\n")
            out.write("    these sizes a bigram web is carried by which words a corpus happens to\n")
            out.write("    hold, and the tree resembles its own scrambled self more than it\n")
            out.write("    resembles either reference. Nothing was read.\n")

    out.write("\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
