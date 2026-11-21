#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# A generated region is reported and attributed, never skipped.
#

import re



# --------------------------------------------------------------------
# GENERATED REGIONS
# --------------------------------------------------------------------
#
# REPORTED AND ATTRIBUTED, NEVER SUPPRESSED, and this is the single decision in this section a
# reader is likely to want to reverse. Here is the evidence against reversing it.
#
# A document can carry BEGIN and END GENERATED marker pairs that CI regenerates. An edit inside one
# is reverted by the next regeneration: the gate reports a fix, the fix disappears, and the finding
# comes back. That argues for skipping the region, and it is the wrong move.
#
# A genuine structural finding, an empty table say, can sit inside a generated region. A rule that
# skips marked regions deletes it and reports the tree clean. A generator that emits a header and a
# separator with no rows under them republishes an empty table on every regeneration. That is a
# generator defect, a person has to fix it in the generator, and the finding is how they find out.
#
# A FINDING INSIDE A MARKED REGION THEREFORE RETAINS ITS PLACE IN THE COUNT and carries the
# generator's name with it. The marker already holds the generator. The attribution is read from the
# document and cannot go stale. What the region buys is an error: a rewrite never goes inside one,
# because writing there is writing to a file CI overwrites.
#
# AND A SOURCE FIX IS PAIRED WITH REGENERATION. Where CI checks generated copies against their
# source, fixing the source without rerunning the generator fails the change that carried the fix.
# Skipping the generated copy is necessary and it is not sufficient. The error below names the
# generator and says to run it.
#
# AN UNCLOSED MARKER IS ITSELF REPORTED. A BEGIN with no END would otherwise annotate the rest of
# the file as generated, the fail-open shape this whole section exists against.
GENERATED_OPEN = re.compile(r"<!--\s*BEGIN GENERATED\b\s*(?P<label>[^>]*?)\s*-->")
GENERATED_CLOSE = re.compile(r"<!--\s*END GENERATED\b")
GENERATED_BY = re.compile(r"\(([^)]+)\)\s*$")


def generated_regions(lines):
    """({line number: generator}, [(line number, complaint)]) for one document.

    The generator is the parenthesized tail of the BEGIN marker where there is one, and the marker's
    label otherwise. A marker may name a path or a command to run.
    """
    inside = {}
    complaints = []
    opened_at = None
    generator = None
    for at, line in enumerate(lines):
        hit = GENERATED_OPEN.search(line)
        if hit:
            if opened_at is not None:
                complaints.append(
                    (
                        opened_at + 1,
                        "BEGIN GENERATED with no END GENERATED under it, and a second "
                        "BEGIN at line %d" % (at + 1),
                    )
                )
            label = hit.group("label").strip()
            named = GENERATED_BY.search(label)
            generator = (
                named.group(1).strip() if named else (label or "an unnamed generator")
            )
            opened_at = at
            continue
        if GENERATED_CLOSE.search(line):
            opened_at = None
            generator = None
            continue
        if opened_at is not None:
            inside[at + 1] = generator
    if opened_at is not None:
        complaints.append(
            (
                opened_at + 1,
                "BEGIN GENERATED with no END GENERATED anywhere under it. Every line "
                "to the end of the file reads as generated",
            )
        )
    return inside, complaints
