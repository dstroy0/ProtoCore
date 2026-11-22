#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Take the machine's own prose out of a session transcript, as a pole with known provenance.
#
#   Usage:  python utils/maint/prose/session_prose.py <transcript.jsonl> [--out <file>]
#
# WHY A TRANSCRIPT BEATS A PUBLISHED CORPUS HERE
#
# Every dataset on a public host carries a model name that nobody outside can verify. A transcript
# needs no label: the machine turns in it were written by the model that wrote them.
#
# WHAT IS TAKEN
#
# Only the text blocks of machine messages. Not the user's turns, which are the other party's
# words. Not tool calls or tool results, which are file contents and command output and would put
# this repository's own text into a pole meant to be independent of it. Not thinking blocks, which
# are a different register from prose written to be read.
#
# THE CONFOUND, AND IT IS A LARGE ONE
#
# The machine wrote this transcript while enforcing a banned phrase list across the tree, and was
# avoiding those phrases in its own messages the whole time. A rate measured here is a rate under
# suppression, and it understates whatever the unconstrained rate would be. It is a floor on the
# register and not an estimate of it. The same fault, in the same direction, as measuring this
# repository right after its banned phrases are scrubbed out.
#
# One session, one task, one reader. Register is technical and agentic throughout. Nothing here
# speaks for how the model writes about anything else.

import io
import json
import os
import re
import sys

FENCED = re.compile(r"```.*?```", re.DOTALL)
INLINE = re.compile(r"`[^`\n]*`")
TABLE = re.compile(r"^\s*\|.*\|\s*$", re.MULTILINE)


def prose_of(said):
    """One message with its code, inline spans and tables removed."""
    text = FENCED.sub(" ", said)
    text = INLINE.sub(" ", text)
    text = TABLE.sub(" ", text)
    return " ".join(text.split())


def machine_text(path):
    """Every text block of every machine message, in order."""
    held = []
    with io.open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except ValueError:
                continue
            message = record.get("message")
            if not isinstance(message, dict):
                continue
            # "assistant" is the transcript's own role label, matched as the record stores it.
            if message.get("role") != "assistant":
                continue
            content = message.get("content")
            if not isinstance(content, list):
                continue
            for block in content:
                if not isinstance(block, dict):
                    continue
                if block.get("type") != "text":
                    continue
                said = block.get("text")
                if isinstance(said, str) and said.strip():
                    held.append(said)
    return held


def main():
    out = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="")
    if len(sys.argv) < 2:
        out.write("  give a transcript path\n")
        out.flush()
        return 1
    path = sys.argv[1]
    target = None
    if "--out" in sys.argv:
        target = sys.argv[sys.argv.index("--out") + 1]

    blocks = machine_text(path)
    cleaned = [prose_of(one) for one in blocks]
    cleaned = [one for one in cleaned if len(one) > 40]
    words = sum(len(one.split()) for one in cleaned)

    out.write("\n  %s\n" % os.path.basename(path))
    out.write("    %d machine messages, %d after code and tables come out\n"
              % (len(blocks), len(cleaned)))
    out.write("    %d words of prose\n" % words)

    if target:
        with io.open(target, "w", encoding="utf-8", newline="\n") as handle:
            for one in cleaned:
                handle.write(one)
                handle.write("\n")
        out.write("    written to %s\n" % target)

    out.write("\n  a rate taken here is a floor: these messages were written while the phrases\n")
    out.write("  being counted were under active suppression.\n\n")
    out.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
