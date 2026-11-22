#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Ask whether the label on a fetched corpus carries any information.
#
#   python utils/maint/prose/oracle_agreement.py           the verdict and the numbers behind it
#   python utils/maint/prose/oracle_agreement.py --quiet   the verdict alone
#
# WHAT THE QUESTION IS
#
# fetch_machine_prose.py takes community uploads that all claim one model generation. Nobody
# can verify that from the outside: a label on a public dataset is a claim by whoever uploaded it,
# and a corpus of some other model's output under that name would look the same from here.
#
# Independent uploads make the claim checkable without trusting any of them. If corpora that
# all claim one model resemble each other more than any of them resembles a corpus known to be
# something else, the label is carrying information. If one sits as far from its own siblings as it
# does from the control, it is either mislabeled or a different register, and a pole built out of
# all of them is a pole built out of two things.
#
# This is the oracle pattern the rest of this work uses, over labels instead of over measurements.
# The agreement is the evidence, and no single corpus is trusted to speak for itself.
#
# WHAT IT DOES NOT ANSWER
#
# It cannot say the label is right. Uploads of the same mislabeled corpus agree perfectly,
# and so do corpora of different models that happen to share a register. It
# catches the ordinary failure: one upload among several that is not what the others are.
#
# The control matters for the same reason. Against a control too close to the subject the
# separation vanishes and nothing is displayed. The control here is the human pole, the furthest
# thing in this tree from machine prose and the pole the distance instrument already
# measures against.
#
# THE CORPORA ARE DATA AND ARE NEVER READ
#
# Third party text off a public host. It is counted and measured, and no line of it is printed.

import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def _repository_root():
    """This repository, asked of git and not inferred from a marker directory.

    A marker the repository produces, such as build/, is absent from a linked worktree and a
    never-built clone, and a climb to it can pass this root and land in another checkout whose paths
    look valid. A marker infers the root. Git answers it. The climb below serves only an exported tree
    with no git directory. It looks for src/cu/engine, which the repository tracks and every checkout
    of it holds.

    Git's own variables are cleared first. Inside a hook GIT_DIR is exported, and a rev-parse that
    inherits it answers about that repository and not about the directory it was asked from,
    returning the current directory instead of the root.
    """
    start = os.path.dirname(os.path.abspath(__file__))
    environment = dict(os.environ)
    for key in (
        "GIT_DIR",
        "GIT_WORK_TREE",
        "GIT_INDEX_FILE",
        "GIT_PREFIX",
        "GIT_COMMON_DIR",
    ):
        environment.pop(key, None)

    try:
        said = subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=start,
            stderr=subprocess.PIPE,
            env=environment,
        )
    except (OSError, subprocess.CalledProcessError):
        said = b""

    top = said.decode("utf-8", "replace").strip()
    if top and os.path.isdir(top):
        return os.path.abspath(top)

    climbed = start
    while (climbed != os.path.dirname(climbed)) and not os.path.isdir(
        os.path.join(climbed, "src", "cu", "engine")
    ):
        climbed = os.path.dirname(climbed)
    return climbed


ROOT = _repository_root()

sys.path.insert(0, HERE)

from machine_distance import (
    distance,
    halves,
    profile,  # noqa: E402
    restricted,
    top_words,
    words_of,
)

APART = os.path.join(ROOT, "build", "corpora", "machine_prose_by_source")
HUMAN = os.path.join(ROOT, "build", "papers")

# A corpus smaller than this cannot carry a distribution and its distances are sampling noise.
LEAST = 2000


def read(path):
    with io.open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def human_text(limit=400000):
    """The human pole, as much of it as is needed to outweigh any one upload."""
    if not os.path.isdir(HUMAN):
        return ""
    held = []
    counted = 0
    for name in sorted(os.listdir(HUMAN)):
        if not name.endswith(".txt"):
            continue
        held.append(read(os.path.join(HUMAN, name)))
        counted += len(held[-1])
        if counted >= limit:
            break
    return "\n".join(held)


def main():
    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    quiet = "--quiet" in sys.argv
    # The numbers behind the verdict. --quiet leaves the verdict alone.
    detail = (lambda _text: None) if quiet else out.write

    detail("\n  %s\n" % APART.replace("\\", "/"))
    if not os.path.isdir(APART):
        out.write(
            "  no per source corpora. Run utils/maint/data/fetch/fetch_machine_prose.py first.\n\n"
        )
        out.flush()
        return 2

    names = sorted(one for one in os.listdir(APART) if one.endswith(".txt"))
    corpora = {}
    for name in names:
        words = words_of(read(os.path.join(APART, name)))
        if len(words) < LEAST:
            out.write(
                "    %-46s %d words, too few to place\n" % (name[:46], len(words))
            )
            continue
        corpora[name[:-4]] = words

    if len(corpora) < 2:
        out.write("  fewer than two usable corpora. Nothing can be compared.\n\n")
        out.flush()
        return 2

    control = words_of(human_text())
    if len(control) < LEAST:
        out.write(
            "  no human pole under build/papers. Without the control a distance does not\n"
        )
        out.write("  mean anything. This stops instead of reporting a bare number.\n\n")
        out.flush()
        return 2

    everything = {one: profile(words)[0] for one, words in corpora.items()}
    everything["control"] = profile(control)[0]
    axis = top_words(list(everything.values()))
    on_axis = {one: restricted(value, axis) for one, value in everything.items()}

    detail(
        "\n  %d corpus(es) claiming one label, against %d control words\n"
        % (len(corpora), len(control))
    )
    for one, words in sorted(corpora.items()):
        detail("    %-52s %7d words\n" % (one[:52], len(words)))

    # Each corpus against its own halves. That is the floor a distance has to clear here.
    detail("\n  own halves, the resolution floor of each\n")
    floors = {}
    for one, words in sorted(corpora.items()):
        first, second = halves(" ".join(words))
        if first is None:
            floors[one] = 0.0
            continue
        left = restricted(profile(words_of(first))[0], axis)
        right = restricted(profile(words_of(second))[0], axis)
        floors[one] = distance(left, right)
        detail("    %-52s %.4f\n" % (one[:52], floors[one]))

    keys = sorted(corpora)
    within = []
    detail("\n  to each other, and to the control\n")
    for at, one in enumerate(keys):
        for other in keys[at + 1 :]:
            value = distance(on_axis[one], on_axis[other])
            within.append((value, one, other))
            detail("    %-30s %-30s %.4f\n" % (one[:30], other[:30], value))
    across = []
    for one in keys:
        value = distance(on_axis[one], on_axis["control"])
        across.append((value, one))
        detail("    %-30s %-30s %.4f\n" % (one[:30], "control", value))

    worst_within = max(within)[0]
    best_across = min(across)[0]
    floor = max(floors.values()) if floors else 0.0

    detail("\n  furthest two that share the label   %.4f\n" % worst_within)
    detail("  nearest one to the control         %.4f\n" % best_across)
    detail("  largest own halves floor           %.4f\n" % floor)

    if worst_within <= floor:
        out.write(
            "\n  every pair sharing the label sits inside its own sampling floor.\n"
        )
        out.write(
            "  Nothing is resolved at this size. Fetch more before trusting the label.\n\n"
        )
        out.flush()
        return 2

    if worst_within < best_across:
        out.write(
            "\n  AGREES. Corpora sharing the label are closer to each other than any is to\n"
        )
        out.write("  the control. The label carries information about the text.\n\n")
        out.flush()
        return 0

    out.write(
        "\n  DISAGREES. At least one corpus sits further from its own siblings than it does\n"
    )
    out.write(
        "  from the control. The label does not separate this text from other writing.\n"
    )
    for value, one, other in sorted(within, reverse=True)[:3]:
        out.write("    %.4f  %s  and  %s\n" % (value, one[:34], other[:34]))
    out.write(
        "  A pole built from all of them would be built out of more than one thing.\n\n"
    )
    out.flush()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
