#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The voice as a rhythm spectrum, and the band a text is graded inside.
#
#   build/tessera_host/tessera_run --processors 1 --name voice_harmonics -- \
#       python utils/maint/prose/docs_check --harmonics
#   from voice_harmonics import Voice
#
# A voice is read here as four signals taken off prose in the order it was written, each one
# through the voice's own tables:
#
#   characters  each character's rank in voice_alphabet.tsv, one sample per character.
#   words       each word's rank in voice.tsv, as log2(1 + rank), one sample per word. A word the
#               voice never used takes the rank past the last one.
#   lengths     each word's length in characters, one sample per word.
#   sentences   each sentence's length in words, one sample per sentence.
#
# A signal's harmonics are its power spectrum. The signal is cut into windows of WINDOW samples
# overlapping by half, each window has its mean taken out and a Hann taper applied, and the power
# of its Fourier transform is averaged over the windows. The constant term is dropped and the other
# bins are scaled to sum to one. Two spectra are compared by total variation, in [0, 1].
#
# A distance means nothing alone. This file also writes the band: blocks of the voice's own text,
# cut at each size in SIZES, each one measured against the spectrum of the other texts only. The
# held-out text is never in the reference it is measured against. A text is outside the voice on a
# signal when its distance is past the EDGE percentile of the held-out blocks of its size.
#
# Writes utils/maint/prose/voice_spectra.tsv (signal, bin, share) and utils/maint/prose/voice_band.tsv
# (signal, words, blocks, median, edge, worst). The ranks are the committed tables' ranks, taken over
# all six texts. A held-out block therefore sees ranks its own text helped set. That moves the
# common ranks by nothing a spectrum can see and is stated here because it is not held out.

import bisect
import glob
import io
import math
import os
import re
import sys

import numpy

# voice_count is a sibling tool and not a member of this package: machine_distance.py, voice_web.py
# and voice_pdftotext.sh read it too, and one copy is the point. Reached by path for that reason.
# It resolves the repository with git at import, so this module is imported where a reading asks
# for it and never from the package root.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import voice_count  # noqa: E402

ROOT = voice_count.ROOT
PROSE = os.path.join(ROOT, "utils", "maint", "prose")
ALPHABET = os.path.join(PROSE, "voice_alphabet.tsv")
WORDS = os.path.join(PROSE, "voice.tsv")
SPECTRA = os.path.join(PROSE, "voice_spectra.tsv")
BAND = os.path.join(PROSE, "voice_band.tsv")

SIGNALS = ("characters", "words", "lengths", "sentences")
WINDOW = {"characters": 256, "words": 64, "lengths": 64, "sentences": 16}
# Block sizes of the band, in words. A text under the smallest is too short to grade.
SIZES = (200, 500, 1000, 2000, 5000, 10000, 20000)
# Blocks cut from each held-out text at each size, evenly spaced through it.
BLOCKS = 40
# A band row needs this many blocks. Four sentence signals at 200 words is a row resting on one page.
MINIMUM_BLOCKS = 20
# The percentile of the held-out blocks a text is graded against. The worst block is retained in the
# table. At 5000 words the sentence signal's median was 0.0735 and its worst 0.8707, set by a few
# outlying blocks, and an edge at the worst would pass nearly anything.
EDGE = 95

SPACES = re.compile(r"\s+")
# A sentence ends at a period, question mark or exclamation mark followed by a space or the end.
SENTENCE_END = re.compile(r"[.!?]+(?=\s|$)")


def plain(text):
    """Text as every voice table reads it, with each run of whitespace read as one space."""
    return SPACES.sub(" ", voice_count.normalized(text))


class Voice:
    """The voice's tables, and the signals and spectra of any text read through them."""

    def __init__(self):
        with io.open(ALPHABET, encoding="utf-8") as handle:
            next(handle)
            self.alphabet = {}
            for line in handle:
                rank, _, code, _ = line.rstrip("\n").split("\t")
                self.alphabet[chr(int(code[2:], 16))] = int(rank)
        with io.open(WORDS, encoding="utf-8") as handle:
            next(handle)
            self.word_rank = {line.split("\t", 1)[0]: place for place, line in enumerate(handle)}
        self.spectra = {}
        self.band = {}
        if os.path.isfile(SPECTRA):
            with io.open(SPECTRA, encoding="utf-8") as handle:
                next(handle)
                for line in handle:
                    signal, _, share = line.rstrip("\n").split("\t")
                    self.spectra.setdefault(signal, []).append(float(share))
            self.spectra = {signal: numpy.array(shares) for signal, shares in self.spectra.items()}
        if os.path.isfile(BAND):
            with io.open(BAND, encoding="utf-8") as handle:
                next(handle)
                for line in handle:
                    signal, words, blocks, median, edge, worst = line.rstrip("\n").split("\t")
                    self.band.setdefault(signal, []).append(
                        (int(words), int(blocks), float(median), float(edge), float(worst)))

    def signals(self, text):
        """The four signals of a text, as float arrays. `text` is already plain()."""
        unseen_character = len(self.alphabet)
        unseen_word = len(self.word_rank)
        characters = [self.alphabet.get(symbol, unseen_character) for symbol in text]
        found = [match.group(0).lower() for match in voice_count.WORD.finditer(text)]
        words = [math.log2(1.0 + self.word_rank.get(word, unseen_word)) for word in found]
        lengths = [len(word) for word in found]
        sentences = []
        for piece in SENTENCE_END.split(text):
            count = len(voice_count.WORD.findall(piece))
            if count:
                sentences.append(count)
        return {
            "characters": numpy.array(characters, dtype=numpy.float64),
            "words": numpy.array(words, dtype=numpy.float64),
            "lengths": numpy.array(lengths, dtype=numpy.float64),
            "sentences": numpy.array(sentences, dtype=numpy.float64),
        }


def power(signal, window):
    """Summed power per frequency bin over half-overlapping Hann windows, and the window count."""
    if len(signal) < window:
        return None, 0
    taper = numpy.hanning(window)
    step = window // 2
    total = numpy.zeros(window // 2)
    count = 0
    for start in range(0, len(signal) - window + 1, step):
        piece = signal[start:start + window]
        piece = (piece - piece.mean()) * taper
        total += numpy.abs(numpy.fft.rfft(piece))[1:window // 2 + 1] ** 2
        count += 1
    return total, count


def shares(total):
    """A power total scaled to sum to one, or None where it holds no power."""
    if total is None:
        return None
    whole = total.sum()
    if whole <= 0.0:
        return None
    return total / whole


def distance(first, second):
    return 0.5 * float(numpy.abs(first - second).sum())


def spectra_of(voice, text):
    """Each signal's spectrum for one text, or None for a signal too short to hold a window."""
    held = voice.signals(text)
    return {signal: shares(power(held[signal], WINDOW[signal])[0]) for signal in SIGNALS}


def grade(voice, text):
    """Each signal's distance to the voice and the band row it is read against.

    Returns (words, rows), one row per signal: (signal, distance or None, band words, median,
    edge). A text under the smallest band size gets no rows.
    """
    text = plain(text)
    words = len(voice_count.WORD.findall(text))
    if words < SIZES[0]:
        return words, []
    mine = spectra_of(voice, text)
    rows = []
    for signal in SIGNALS:
        band = voice.band.get(signal, [])
        sizes = [one[0] for one in band]
        at = bisect.bisect_right(sizes, words) - 1
        if (mine[signal] is None) or (at < 0) or (signal not in voice.spectra):
            rows.append((signal, None, 0, 0.0, 0.0))
            continue
        size, _, median, edge, _ = band[at]
        rows.append((signal, distance(mine[signal], voice.spectra[signal]), size, median, edge))
    return words, rows


def show_harmonics():
    voice = Voice()
    sources = sorted(glob.glob(os.path.join(voice_count.TEXT, "*.txt")))
    if not sources:
        sys.exit("no text in %s; run voice_pdftotext.sh under tessera_run first" % voice_count.TEXT)
    texts = []
    for path in sources:
        with io.open(path, encoding="utf-8", errors="replace") as handle:
            texts.append((os.path.basename(path), plain(handle.read())))

    totals = []
    for name, text in texts:
        held = voice.signals(text)
        totals.append({signal: power(held[signal], WINDOW[signal])[0] for signal in SIGNALS})
        print("  %8d words  %s" % (len(held["words"]), name))

    whole = {signal: shares(sum(one[signal] for one in totals)) for signal in SIGNALS}
    with io.open(SPECTRA, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("signal\tbin\tshare\n")
        for signal in SIGNALS:
            for place, share in enumerate(whole[signal], start=1):
                handle.write("%s\t%d\t%.9f\n" % (signal, place, share))

    readings = {(signal, size): [] for signal in SIGNALS for size in SIZES}
    for index, (name, text) in enumerate(texts):
        others = {signal: shares(sum(one[signal] for at, one in enumerate(totals) if at != index))
                  for signal in SIGNALS}
        spans = [match.span() for match in voice_count.WORD.finditer(text)]
        for size in SIZES:
            if len(spans) < size:
                continue
            last = len(spans) - size
            count = min(BLOCKS, 1 + last // size)
            for number in range(count):
                first = (last * number) // max(1, count - 1) if count > 1 else 0
                block = text[spans[first][0]:spans[first + size - 1][1]]
                mine = spectra_of(voice, block)
                for signal in SIGNALS:
                    if mine[signal] is not None:
                        readings[(signal, size)].append(distance(mine[signal], others[signal]))

    with io.open(BAND, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("signal\twords\tblocks\tmedian\tedge\tworst\n")
        for signal in SIGNALS:
            for size in SIZES:
                found = readings[(signal, size)]
                if len(found) < MINIMUM_BLOCKS:
                    continue
                row = (float(numpy.median(found)), float(numpy.percentile(found, EDGE)), max(found))
                handle.write("%s\t%d\t%d\t%.6f\t%.6f\t%.6f\n" % ((signal, size, len(found)) + row))
                print("  %-10s %6d words  %4d blocks  median %.4f  edge %.4f  worst %.4f"
                      % ((signal, size, len(found)) + row))
