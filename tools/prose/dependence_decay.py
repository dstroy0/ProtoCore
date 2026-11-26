"""Does the text remember? Mutual information against lag, and the shape of its decay.

THE QUESTION, AND WHY A FILE-LEVEL DISTANCE CANNOT ANSWER IT

`machine_distance` places a file between a human pole and a machine pole, and prints its own limit at the foot. Every
file-level distance is a bag of words, and it therefore reads COMPOSITION and never ARRANGEMENT. A bag of words does not
change under permutation: shuffle the corpus, and every count stays identical. It therefore cannot separate a text from
its own shuffle, and it cannot say WHICH sentences carry the register. That is not a flaw in the tool. It is what a bag
of words is.

The gap is what this measures. Generated text is close to MEMORYLESS. A model samples it token by token from a context
window, and its long-range structure is therefore whatever that window carries. Human writing is not memoryless. A
person holds a subject across paragraphs and returns to it, and dependence then lasts to ranges that no window covers.

The discriminator is therefore not a score. It is a SHAPE: how fast dependence dies as the gap grows.

THE STATISTIC

Mutual information between the token at t and the token at t+k, swept in k:

    I(k) = sum over (x, y) of p(x,y) log2( p(x,y) / (p(x) p(y)) )

I(k) is zero exactly when the two positions are independent. Lin and Tegmark measured this shape for natural language
against Markov sources. They found the separation in the DECAY LAW. Natural language falls off as a power law, and
anything with a bounded context falls off exponentially and hits the floor. A power law is straight on log-log, and an
exponential is straight on log-linear and drops off a cliff. A curve can show that shape, and a single number cannot.

This is the same instrument as the autocorrelation sweep of `bench_walk` over lags 1 to 1024, pointed at text instead of
nonces. It answers the same question: does this thing carry anything across a gap, or does it forget.

THE NULL IS FREE, AND THE RUN DRAWS IT AND DOES NOT DERIVE IT

At finite sample, the estimate of MI has an UPWARD bias. With V*V cells and limited counts, independent data still
produces a positive number, and the bias grows as the table gets sparse. A derivation of that bias is possible, and this
tree has a rule against it. A derived bar comes out too low, always in the same direction.

The run therefore measures it. A shuffle of the tokens destroys arrangement and keeps every count identical. That makes
a shuffle EXACTLY the memoryless surrogate for this statistic. The floor at each lag is what the shuffle produces at
that lag, drawn several times. The report gives the excess over that floor. A decay to zero means a decay to where this
corpus at this size can no longer tell.

THE POSITIVE CONTROL IS THE POINT, NOT AN EXTRA

Both poles already exist in the tree, and this builds on them and rents nothing. Human research papers are one, and the
machine's own transcript is the other. If the human corpus does not show a heavier tail than the machine corpus, the
instrument does not measure what it claims. No reading from it then means anything. That check runs first, and its
verdict gates the rest, the same way pi in the compressor gates a compression claim.

WHAT BOUNDS A READING

The decay law is the right discriminator, and long-range dependence is real in natural language. A null from this
instrument is therefore a statement about its power at a given corpus size, and not about the effect. Three nuisance
parameters bound that power, and each one has a control.

LENGTH. MI bias grows as a corpus shrinks, and every comparison happens at the length of the shorter pole. At that
length the human decay slopes form a band, and only a separation wider than that band is reportable. The band narrows as
the root of the length: more text is the way forward, and not a better statistic. `--band` draws the human band at a
chosen `--length`.

THE UNIGRAM DISTRIBUTION. The excess at any one lag carries the estimator's bias: a bare LEVEL compares two estimators
wherever the unigram distributions differ. The hypothesis is about SHAPE instead of level. A division of each curve by
its own value at lag one removes the scale that the bias sets. A power law and an exponential differ in how they fall,
whatever that scale is. `--twoband` gives both poles a band at a common length, and measures the confound alongside.

ASSEMBLY. `load_papers` concatenates about 120 separate documents, and different papers use different words at different
rates. Composition drifts across the joins, and drift raises MI at every lag. A position in the corpus tells which
document it is in, and therefore which words are likely. That is a fact about how the corpus comes together, and not
about the writing. Document count and join rate set the large-lag excess of a corpus on their own.

SEPARATING DRIFT FROM DEPENDENCE

Drift and real dependence come apart exactly. `--scramble` shuffles the tokens inside every window of 100. That destroys
all arrangement shorter than 100 words, and leaves composition above that scale untouched. Real dependence at lag 256
cannot survive it, and it does not affect drift. Where the large-lag excess survives the scramble, the tail is drift,
and not a long-range signal.

The inversion deserves a plain statement, because it reverses the premise that the line starts from. A continuous
single-author work is the arm with the LEAST large-lag excess, and concatenated corpora have the max. Large-k excess
tracks how a corpus comes together, and is close to silent about the writing inside it.

WHAT A NULL ON THE SHAPE MEANS

A wider machine spread is itself evidence against the mechanism, and not a failure to detect it. The hypothesis says
that generated text is closer to memoryless: its decay should be tighter and steeper. A spread wider than the human band
argues against the mechanism directly. Every word of the machine pole comes under the ban list of this tree. That list
suppresses its register in the direction that works against a separation. A null on the shape is therefore ambiguous
about unsuppressed prose, though the spread argument does not depend on the suppression.

    `python tools/prose/dependence_decay.py --control`
    `python tools/prose/dependence_decay.py --band`
    `python tools/prose/dependence_decay.py --twoband --length 200000`
    `python tools/prose/dependence_decay.py --scramble`
    `python tools/prose/dependence_decay.py --file theory/workbooks/orior/aiming-the-engine.md`
"""

import argparse
import glob
import io
import os
import random
import re
import sys

import numpy

HERE = os.path.dirname(os.path.abspath(__file__))


def _anchor(start):
    """Walk up until the directory that holds src/cu/engine turns up, in place of a count of parents.

    A fixed number of dirname calls encodes how deep this file happens to sit. A search upward finds it wherever this
    file sits.
    """
    at = start
    while at != os.path.dirname(at):
        if os.path.isdir(os.path.join(at, "src", "cu", "engine")):
            return at
        at = os.path.dirname(at)
    return start


ANCHOR = _anchor(HERE)
PAPERS = os.path.join(ANCHOR, "build", "papers")
SESSION = os.path.join(ANCHOR, "build", "corpora", "session_prose.txt")

WORD = re.compile(r"[a-z']+")

# Lags are log spaced because the expected law is a power law, and the axis it reads on is
# multiplicative. Sampling 1..64 evenly would spend every point where the curve is steep and none
# where the two hypotheses separate.
LAGS = (1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024)


def tokens_of(text, vocabulary=256):
    """Words as integer symbols, with the commonest `vocabulary` kept and the rest merged.

    A JOINT TABLE IS V SQUARED CELLS, AND THE COUNTS MUST FILL IT. With a full vocabulary the table is emptier than the
    data, and every cell holds zero or one. The estimate is then almost pure bias. The shuffle floor sits on top of the
    signal, and nothing shows above it. To keep the commonest few hundred words trades resolution for a table that the
    corpus can actually fill. That is a choice about the estimator and not about the text, and every corpus compared
    here gets the same choice.
    """
    words = WORD.findall(text.lower())
    counts = {}
    for word in words:
        counts[word] = counts.get(word, 0) + 1
    common = sorted(counts, key=lambda w: -counts[w])[: vocabulary - 1]
    index = {word: number for number, word in enumerate(common)}
    # Everything outside the retained vocabulary shares the last symbol.
    other = vocabulary - 1
    return numpy.fromiter((index.get(w, other) for w in words), dtype=numpy.int32, count=len(words)), vocabulary


def mutual_information(symbols, lag, size):
    """I(X_t ; X_t+lag) in bits, from the joint counts at that lag."""
    if lag >= len(symbols):
        return 0.0
    left = symbols[:-lag]
    right = symbols[lag:]
    total = float(left.size)

    joint = numpy.bincount(left.astype(numpy.int64) * size + right, minlength=size * size).astype(numpy.float64)
    joint /= total
    joint = joint.reshape(size, size)

    # The marginals come from the SAME pairs, and the whole sequence does not supply them. The estimate is then of the
    # pair distribution actually observed, and the shuffle floor is comparable.
    px = joint.sum(axis=1)
    py = joint.sum(axis=0)
    outer = numpy.outer(px, py)

    live = joint > 0.0
    return float(numpy.sum(joint[live] * numpy.log2(joint[live] / outer[live])))


def curve(symbols, size, draws=3, seed=0x5EED):
    """MI at each lag, the shuffle floor at each lag, and the excess."""
    rng = numpy.random.default_rng(seed)
    out = []
    shuffled = symbols.copy()
    for lag in LAGS:
        real = mutual_information(symbols, lag, size)
        floors = []
        for _ in range(draws):
            rng.shuffle(shuffled)
            floors.append(mutual_information(shuffled, lag, size))
        floor = sum(floors) / len(floors)
        out.append((lag, real, floor, real - floor))
    return out


def show(name, rows, words):
    print()
    print("  %s   %s words" % (name, format(words, ",")))
    print("    %6s %12s %12s %12s" % ("lag", "MI bits", "shuffle", "excess"))
    for lag, real, floor, excess in rows:
        mark = "" if excess > floor * 0.25 else "   <- at the floor"
        print("    %6d %12.6f %12.6f %12.6f%s" % (lag, real, floor, excess, mark))


def decay_shape(rows):
    """The log-log slope of the excess against lag: the statistic that the hypothesis is about.

    WHY SHAPE AND NOT LEVEL. The excess at any one lag carries the estimator's bias. That bias depends on corpus size
    and on the unigram distribution, and neither of them is register. A comparison of levels across two corpora compares
    their estimators. But the HYPOTHESIS is not about level. A power law and an exponential differ in how they fall, and
    that difference survives an overall scale factor.

    The code therefore divides each curve by its own value at lag one, and takes the slope in log-log. A power law is a
    straight line there, with the exponent as the slope. An exponential curves downward and runs off the bottom. The
    division by lag one removes the scale that the bias sets. It is the same normalization as the division of per-degree
    power by (2l+1) to make a spectrum flat.

    Returned as (slope, points used). The code drops lags whose excess is zero or less, and does not clamp them. A
    logarithm of a negative number is not a small number: it is nothing.
    """
    import math

    base = rows[0][3]
    if base <= 0.0:
        return None, 0
    xs = []
    ys = []
    for lag, real, floor, excess in rows:
        if excess <= 0.0:
            continue
        xs.append(math.log(float(lag)) if lag > 0 else 0.0)
        ys.append(math.log(excess / base))
    if len(xs) < 4:
        return None, len(xs)
    n = float(len(xs))
    mx = sum(xs) / n
    my = sum(ys) / n
    top = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    bottom = sum((x - mx) ** 2 for x in xs)
    if bottom <= 0.0:
        return None, len(xs)
    return top / bottom, len(xs)


def reach(rows):
    """The largest lag whose excess is still clear of the shuffle floor.

    "Clear" is a quarter of the floor, where a sigma would be the usual bar. The floor here is a bias, and not a noise
    band. Three shuffles agree with each other closely, and the spread understates how far the estimate can sit from
    truth. A fraction of the bias is the honest bar. This docstring states the bar in the open, and nothing tunes it.
    """
    best = 0
    for lag, real, floor, excess in rows:
        if excess > floor * 0.25:
            best = lag
    return best


def load_papers(limit_words=400000):
    parts = []
    have = 0
    for path in sorted(glob.glob(os.path.join(PAPERS, "*.txt"))):
        try:
            body = io.open(path, encoding="utf-8", errors="replace").read()
        except Exception:
            continue
        parts.append(body)
        have += len(body.split())
        if have >= limit_words:
            break
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Dependence decay against lag.")
    parser.add_argument("--control", action="store_true", help="human pole against machine pole")
    parser.add_argument("--band", action="store_true", help="draw the human slope band from disjoint equal-size chunks")
    parser.add_argument("--scramble", action="store_true", help="is the human tail dependence, or document boundaries?")
    parser.add_argument(
        "--twoband", action="store_true", help="a band for each pole, both cut to --length, with the confound measured"
    )
    parser.add_argument(
        "--length", type=int, default=200000, help="tokens per chunk; see the note on why longer is not better"
    )
    parser.add_argument("--file", default=None)
    parser.add_argument("--vocabulary", type=int, default=256)
    given = parser.parse_args()

    print("=" * 78)
    print("  DOES THE TEXT REMEMBER? Mutual information against lag.")
    print("=" * 78)

    if given.scramble:
        # IS THE HUMAN TAIL DEPENDENCE, OR IS IT DOCUMENT BOUNDARIES?
        #
        # load_papers concatenates about 120 separate documents. Different papers use different words at different
        # rates, and the COMPOSITION drifts across the joins. Drift raises MI at every lag, because a position in the
        # corpus tells which document it is in, and therefore which words are likely. That is not dependence at a
        # distance. It is the assembly of the corpus.
        #
        # The two are separable, and the test is exact. A scramble of the tokens INSIDE each window of 100 destroys
        # every arrangement shorter than 100 words, and leaves composition above that scale untouched. Real dependence
        # at lag 256 cannot survive that, and the scramble does not affect drift. If the large-lag excess is still up
        # after the scramble, the curve does not read dependence.
        span = 100
        human, size = tokens_of(load_papers(limit_words=400000), given.vocabulary)
        plain = curve(human, size, draws=3)

        rng = numpy.random.default_rng(0xA55E)
        mixed = human.copy()
        for at in range(0, mixed.size - span, span):
            piece = mixed[at : at + span]
            rng.shuffle(piece)
            mixed[at : at + span] = piece
        scrambled = curve(mixed, size, draws=3)

        print()
        print("  human pole, %s tokens, scrambled within every %d" % (format(human.size, ","), span))
        print()
        print("    %6s %12s %12s %12s %12s" % ("lag", "as-is", "its floor", "scrambled", "its floor"))
        for (lag, r1, f1, e1), (_, r2, f2, e2) in zip(plain, scrambled):
            print("    %6d %12.6f %12.6f %12.6f %12.6f" % (lag, e1, f1, e2, f2))

        print()
        print("=" * 78)
        print("  WHAT THE TAIL WAS")
        print("=" * 78)
        print()
        big = [(lag, e1, e2) for (lag, _, _, e1), (_, _, _, e2) in zip(plain, scrambled) if lag >= 256]
        survived = sum(1 for lag, e1, e2 in big if e2 >= e1 * 0.5)
        for lag, e1, e2 in big:
            print(
                "    lag %4d   as-is %.6f   scrambled %.6f   %s"
                % (lag, e1, e2, "SURVIVED" if e2 >= e1 * 0.5 else "destroyed")
            )
        print()
        if survived >= len(big) - 1:
            print("    The large-lag excess survives having every arrangement below %d words" % span)
            print("    destroyed: it is not dependence at a distance. It is composition drift")
            print("    across the documents this corpus was concatenated from.")
            print()
            print("    So the human tail reported earlier is an artifact of corpus ASSEMBLY, and")
            print("    the claim it supported - that long-range structure lives in content-word")
            print("    recurrence - loses that support. Retracted here and nowhere else.")
        else:
            print("    The large-lag excess is destroyed by scrambling: it was dependence at a")
            print("    distance after all and survives this check.")
        return 0

    if given.twoband:
        # BOTH POLES GET A BAND.
        #
        # A point cannot say whether it is a typical value for its own population or a draw from a wide one. One number
        # for a pole cannot tell "the machine decays faster" from "the machine sample happened to". With both bands
        # measured, the question becomes the right one. The answer can also say that the machine spread is as wide as
        # the human one. That would sink the hypothesis, whatever the means do.
        #
        # WHY NOT SIMPLY USE EVERY WORD. The band narrows as 1/sqrt(L), and the number of DISJOINT chunks to estimate it
        # from falls as 1/L. Those two fight. At a large enough length there are too few DISJOINT chunks, and the spread
        # is no longer measurable. The output then prints a large separation against an unmeasurable band, and that
        # looks like the best result. The default sits where both are adequate.
        machine_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "build", "corpora", "machine_suppressed.txt"
        )
        if not os.path.exists(machine_path):
            print("  no machine corpus at %s" % machine_path)
            return 1

        length = given.length
        human, size = tokens_of(load_papers(limit_words=7000000), given.vocabulary)
        machine, size = tokens_of(io.open(machine_path, encoding="utf-8", errors="replace").read(), given.vocabulary)

        def bands(symbols, label, cap):
            out = []
            at = 0
            while at + length <= symbols.size and len(out) < cap:
                piece = symbols[at : at + length]
                # The top-256 mass share is the nuisance that the floors exposed: it sets how the joint table fills, and
                # therefore the bias. The run records it per chunk, and can then MEASURE its influence below, and not
                # correct for it on faith.
                share = float(numpy.count_nonzero(piece != size - 1)) / float(piece.size)
                slope, _ = decay_shape(curve(piece, size, draws=2, seed=0x5EED + at))
                if slope is not None:
                    out.append((slope, share))
                at += length
            print("    %-10s %2d chunks of %s tokens" % (label, len(out), format(length, ",")))
            return out

        print()
        print("  cutting both poles into disjoint chunks")
        human_rows2 = bands(human, "human", 40)
        machine_rows2 = bands(machine, "machine", 40)
        print()

        if len(human_rows2) < 4 or len(machine_rows2) < 3:
            print("  NOT ENOUGH DISJOINT CHUNKS at this length to measure a spread. Lower --length.")
            return 1

        def summarise(rows):
            values = [v for v, _ in rows]
            mean = sum(values) / len(values)
            sd = (sum((v - mean) ** 2 for v in values) / max(len(values) - 1, 1)) ** 0.5
            shares = [s for _, s in rows]
            return mean, sd, len(values), sum(shares) / len(shares)

        hm, hs, hn, hshare = summarise(human_rows2)
        mm, msd, mn, mshare = summarise(machine_rows2)

        print("    %-10s slope %+.4f  sd %.4f  n %2d   top-%d mass share %.4f" % ("human", hm, hs, hn, size, hshare))
        print("    %-10s slope %+.4f  sd %.4f  n %2d   top-%d mass share %.4f" % ("machine", mm, msd, mn, size, mshare))
        print()

        # DOES THE NUISANCE ACTUALLY DRIVE THE SLOPE? The run measures it across every chunk of both poles pooled. If
        # mass share and slope have no correlation, the difference in share between the poles cannot produce a
        # difference in slope. No match is then necessary.
        pooled = human_rows2 + machine_rows2
        xs = [s for _, s in pooled]
        ys = [v for v, _ in pooled]
        mx = sum(xs) / len(xs)
        my = sum(ys) / len(ys)
        sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
        sxx = sum((x - mx) ** 2 for x in xs)
        syy = sum((y - my) ** 2 for y in ys)
        r = sxy / ((sxx * syy) ** 0.5) if sxx > 0 and syy > 0 else 0.0
        print("    slope against mass share, across all %d chunks:  r = %+.3f" % (len(pooled), r))
        if abs(r) < 0.3:
            print("      weak: the share difference between the poles is not what moves the")
            print("      slope and matching on it would change nothing")
        else:
            print("      STRONG. The nuisance tracks the statistic: any separation below may be")
            print("      the mass share talking. Match on it before believing the comparison.")
        print()

        # Welch, because the two bands have different sizes and different spreads, and a pool of them would assume the
        # thing under test.
        se = ((hs * hs / hn) + (asd * asd / an)) ** 0.5
        gap = am - hm
        print("=" * 78)
        print("  RESULT")
        print("=" * 78)
        print()
        print("    gap %+.4f, standard error %.4f: %.2f sd apart" % (gap, se, abs(gap) / se if se > 0 else 0.0))
        print()
        overlap = sum(1 for v, _ in human_rows2 if v <= am)
        print("    %d of %d human chunks decay at least as fast as the machine MEAN" % (overlap, hn))
        print()
        if se > 0 and abs(gap) / se >= 3.0 and gap < 0 and abs(r) < 0.3:
            print("    The machine pole decays faster, the separation is %.1f sd on two measured" % (abs(gap) / se))
            print("    bands, and the nuisance does not track the statistic. That is a result.")
            print()
            print("    And it is CONSERVATIVE: every word of the machine corpus was written under")
            print("    the ban list: the register is suppressed in the direction that works")
            print("    against this separation. The unsuppressed pole would only widen it.")
        elif gap < 0 and se > 0 and abs(gap) / se >= 2.0:
            print("    Suggestive in the predicted direction at %.1f sd, which is not enough to" % (abs(gap) / se))
            print("    call on a statistic with this much machinery under it. More machine text")
            print("    or a larger --length is the move, subject to the chunk-count tradeoff.")
        else:
            print("    NO SEPARATION. The two bands overlap: the decay shape does not")
            print("    distinguish these corpora at this length and this is a null.")
        return 0

    if given.band:
        # THE SLOPE COMPARISON NEEDS A NULL, AND THIS DRAWS IT.
        #
        # One human slope against one machine slope is two numbers. The gap between them means nothing without a measure
        # of how much a human slope varies from one sample to the next. The run therefore cuts the human corpus into
        # DISJOINT chunks of exactly the length of the machine corpus, and takes a slope from each. That is the band
        # that a human text of this size produces, drawn and not argued. The slope of the machine either sits inside it
        # or does not.
        session_text = io.open(SESSION, encoding="utf-8", errors="replace").read()
        session, size = tokens_of(session_text, given.vocabulary)
        chunk = int(session.size)

        human_text = load_papers(limit_words=2000000)
        human, size = tokens_of(human_text, given.vocabulary)

        slopes = []
        at = 0
        while at + chunk <= human.size and len(slopes) < 24:
            piece = human[at : at + chunk]
            slope, _ = decay_shape(curve(piece, size, draws=2, seed=0x5EED + at))
            if slope is not None:
                slopes.append(slope)
            at += chunk

        session_slope, _ = decay_shape(curve(session, size, draws=2))

        slopes.sort()
        middle = sum(slopes) / len(slopes)
        spread = (sum((v - middle) ** 2 for v in slopes) / len(slopes)) ** 0.5
        below = sum(1 for v in slopes if v <= session_slope)

        print()
        print("  %d disjoint human chunks of %s words each" % (len(slopes), format(chunk, ",")))
        print()
        print("    human slopes    %.3f to %.3f, mean %.3f, sd %.3f" % (slopes[0], slopes[-1], middle, spread))
        print("    machine slope   %.3f" % session_slope)
        print()
        z = (session_slope - middle) / spread if spread > 0 else 0.0
        print("    the machine sits %.2f sd from the human mean, and %d of %d human chunks" % (z, below, len(slopes)))
        print("    fall at or below it")
        print()
        if below == 0:
            print("    OUTSIDE THE BAND. No human chunk of this size decays as fast as the")
            print("    machine corpus does. That is a real separation on a drawn null.")
        elif below <= max(1, len(slopes) // 20):
            print("    AT THE EDGE. Only %d of %d human chunks reach it: this is suggestive" % (below, len(slopes)))
            print("    and would want a larger human sample before it is called.")
        else:
            print("    INSIDE THE BAND. %d of %d human chunks decay at least as fast: the" % (below, len(slopes)))
            print("    machine slope is an ordinary value for a human text of this length and")
            print("    the shape does NOT separate the poles. The -0.619 against -0.576 seen")
            print("    earlier was within the ordinary spread and is not a finding.")
        return 0

    if given.control or not given.file:
        if not os.path.isdir(PAPERS):
            print("  no human corpus at %s" % PAPERS)
            return 1
        human_text = load_papers()
        human, size = tokens_of(human_text, given.vocabulary)
        human_rows = curve(human, size)
        show("HUMAN POLE, research papers", human_rows, human.size)

        if not os.path.exists(SESSION):
            print("  no machine corpus at %s" % SESSION)
            return 1
        session_text = io.open(SESSION, encoding="utf-8", errors="replace").read()
        session, size = tokens_of(session_text, given.vocabulary)
        session_rows = curve(session, size)
        show("MACHINE POLE, own transcript", session_rows, session.size)

        # SIZE IS THE NUISANCE PARAMETER, AND IT MUST STAY FIXED.
        #
        # The poles differ by eleven times in length, and their shuffle floors differ by five times. MI bias grows as a
        # corpus shrinks and its joint table empties. A comparison across that gap would report the LENGTH of the two
        # corpora and call it register.
        #
        # The run therefore cuts the human pole to the exact token count of the machine pole, and compares them there.
        # The full-length human curve above stays only to show what the instrument can see with enough text. The verdict
        # does not rest on it.
        matched = human[: session.size]
        matched_rows = curve(matched, size)
        show("HUMAN POLE, cut to the machine pole's length", matched_rows, matched.size)

        human_reach = reach(matched_rows)
        session_reach = reach(session_rows)
        print()
        print("=" * 78)
        print("  THE CONTROL, AT EQUAL LENGTH")
        print("=" * 78)
        print()
        print("    both corpora cut to %s words" % format(int(session.size), ","))
        human_floor = matched_rows[0][2]
        session_floor = session_rows[0][2]
        apart = abs(human_floor - session_floor) / max(human_floor, session_floor)
        print("    human floor %.4f, machine floor %.4f, %.0f%% apart" % (human_floor, session_floor, apart * 100.0))
        print()

        # ERROR, AND DO NOT PRINT THE WARNING AND CARRY ON.
        #
        # A gate that states the floor-match requirement and then reports a verdict regardless only advises, and does
        # not stop. That is the same fail-open shape as a linker that reports "created" and does not check that the link
        # resolves.
        #
        # The floors are the estimator's bias at each corpus, and equal length does NOT equalize them. Bias depends on
        # how the joint table fills, and the unigram distribution sets that. Two corpora whose commonest words carry
        # different mass produce different bias at identical token counts. A comparison of an excess over floor across
        # floors that do not match compares two different estimators, and reports the difference between THEM. THE SHAPE
        # IS COMPARABLE EVEN WHERE THE LEVEL IS NOT. The run computes it either way, and the verdict rests on it when
        # the floors do not match.
        human_slope, human_points = decay_shape(matched_rows)
        session_slope, session_points = decay_shape(session_rows)
        print("    decay shape, log-log slope of the excess normalized to its own lag one:")
        print(
            "      human      %s  over %d lags"
            % (("%+.3f" % human_slope) if human_slope is not None else "  n/a", human_points)
        )
        print(
            "      machine    %s  over %d lags"
            % (("%+.3f" % session_slope) if session_slope is not None else "  n/a", session_points)
        )
        print()

        if apart > 0.10:
            print("    THE LEVELS ARE NOT COMPARABLE. The floors sit %.0f%% apart: an excess" % (apart * 100.0))
            print("    over them measures the two estimators and not the two corpora, and no")
            print("    verdict is returned from the lag-reach numbers.")
            print()
            if human_slope is not None and session_slope is not None:
                if session_slope < human_slope:
                    print("    THE SHAPE STILL SEPARATES THEM, and it is the statistic the hypothesis")
                    print(
                        "    was about. The machine curve falls away faster, %+.3f against %+.3f,"
                        % (session_slope, human_slope)
                    )
                    print("    the direction predicted: bounded context forgets sooner. The")
                    print("    normalization divides each curve by its own lag one: the bias scale")
                    print("    cancels to first order and this survives the mismatch above.")
                    print()
                    print("    Treat it as SUGGESTIVE and not settled. The cancellation is first")
                    print("    order only, and the shape has no drawn null yet - fade a known-register")
                    print("    sample past it before it carries a verdict, the way bench_walk's")
                    print("    injection floor turned an unbounded null into 0.0625.")
                else:
                    print(
                        "    AND THE SHAPE DOES NOT SEPARATE THEM EITHER, %+.3f against %+.3f."
                        % (session_slope, human_slope)
                    )
                    print("    The instrument has nothing to say about these two corpora yet.")
            print()
            print("    Length is matched and this persists: length was not the whole nuisance")
            print("    parameter. The remaining one is the unigram distribution: the machine")
            print("    corpus concentrates more mass in its commonest words, which fills the joint")
            print("    table differently and moves the bias. Matching that - by sampling both to a")
            print("    common unigram distribution, or by using an estimator whose bias does not")
            print("    depend on it - is the work this instrument still needs.")
            print()
            print("    WHAT THE FULL-LENGTH RUN ABOVE IS AND IS NOT. Human dependence reaching lag")
            print("    1024 against the machine's lag 2 is almost entirely the size difference,")
            print("    eleven times, and it is not evidence of anything about register. It is")
            print("    reported here so nobody quotes it later as though it were.")
            return 1
        print()
        print("    human dependence is clear of its floor out to lag      %d" % human_reach)
        print("    machine dependence is clear of its floor out to lag    %d" % session_reach)
        print()
        if human_reach > session_reach:
            print("    The human corpus carries dependence further AT THE SAME LENGTH: the")
            print("    separation is a property of the writing and not of how much of it there is.")
            print("    The instrument separates the poles in the direction the hypothesis")
            print("    predicts, and a reading from it means something.")
        else:
            print("    THE INSTRUMENT DOES NOT SEPARATE THE POLES. Every reading from it is void")
            print("    until that is fixed. Do not interpret any file with it.")
            print()
            print("    The corpora differ enormously in size, and MI bias grows as a corpus")
            print("    shrinks: the likeliest cause is the comparison instead of the text.")
            return 1
        return 0

    text = io.open(given.file, encoding="utf-8", errors="replace").read()
    symbols, size = tokens_of(text, given.vocabulary)
    rows = curve(symbols, size)
    show(given.file, rows, symbols.size)
    print()
    print("    dependence is clear of the floor out to lag %d" % reach(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
