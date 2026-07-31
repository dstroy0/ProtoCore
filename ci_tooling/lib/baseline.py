#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A ratchet for rules the tree does not satisfy yet.

A checker written for an existing codebase has two bad options: stay out of CI until
the cleanup lands, so nothing stops the count growing, or fail the build immediately,
so someone disables it. Both end with the rule unenforced.

The third option is a recorded floor. `save()` writes today's violations; `filter_new()`
returns only the ones that are not in it. CI fails on new violations while the known
set is worked down, and re-running `save()` after a sweep lowers the floor. The count
can go down and never up.

Do not add an entry to a baseline to make a build pass. That is silencing an alarm
with extra steps, and it is the failure this file exists to make unnecessary - fixing
one violation is always available and always cheaper than arguing with the gate.

Both check_symbols.py and check_src_banned.py use this. It lives here because the
second checker to need a ratchet is the moment a second implementation gets written,
and two ratchets that disagree about what "known" means is worse than none.
"""

import json
import os


def path_for(script_file, name=None):
    """Baseline file beside the checker that owns it."""
    d = os.path.dirname(os.path.abspath(script_file))
    stem = name or (os.path.splitext(os.path.basename(script_file))[0] + "_baseline")
    return os.path.join(d, stem + ".json")


def load(path):
    """The recorded set, or an empty set when there is no baseline yet."""
    if not os.path.exists(path):
        return set()
    with open(path, encoding="utf-8") as fh:
        return set(json.load(fh))


def save(path, keys):
    """Record the current violation set as the floor.

    `keys` is materialized once: it is usually a generator, and consuming it a second
    time to count yields an empty set - which reported "recorded 0" while writing a
    full file. A count that disagrees with what was written is worse than no count.
    """
    keys = set(keys)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        json.dump(sorted(keys), fh, indent=1)
        fh.write("\n")
    return len(keys)


def filter_new(findings, key_of, path):
    """(new, known_remaining, fixed_since_baseline).

    `fixed` is reported so a sweep can show progress and so a baseline that has drifted
    far below the tree is visible rather than quietly generous.
    """
    known = load(path)
    keys = [key_of(f) for f in findings]
    new = [f for f, k in zip(findings, keys) if k not in known]
    still = set(keys) & known
    return new, len(still), len(known) - len(still)
