#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Harvest symbols from src/ headers, so checkers compare docs against the code.

Written for check_examples.py and reused by the naming-law checker: both need to
know what the code actually declares rather than what a document claims. Keeping
one harvester means the two cannot disagree about, say, which enum a member
belongs to.
"""

import os
import re

import doc_region as dr

_BLOCK = re.compile(r"/\*.*?\*/", re.S)
_LINE = re.compile(r"//[^\n]*")


def blank_comments(text):
    """Replace comments with spaces, PRESERVING every newline.

    Line numbers are the whole point of a checker's output, and a substitution that
    deletes a block comment silently shifts every line after it - check_symbols
    reported a namespace at line 73 that actually sat at 108. Blanking instead of
    deleting keeps offsets exact, and keeps comment text from matching code patterns.
    """
    def _blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return _LINE.sub(_blank, _BLOCK.sub(_blank, text))


# retained name; same line-preserving behavior
def _decomment(text):
    return blank_comments(text)


def headers(root=None):
    """Every .h under src/, absolute paths."""
    root = root or dr.repo_root(__file__)
    out = []
    for base, dirs, files in os.walk(os.path.join(root, "src")):
        for f in files:
            if f.endswith(".h"):
                out.append(os.path.join(base, f))
    return sorted(out)


def enum_members(root=None):
    """{MEMBER: {owning enum class, ...}}.

    A member owned by more than one enum class is ambiguous and callers must not
    guess: `NONE`, `PENDING`, and `START` each appear in two unrelated enums.

    Two things the body match has to get right, both learned the hard way:

    * The body is `[^{}]*`, not `.*?`. A lazy dot-star runs past the closing brace
      when a later construct interferes, silently attributing one enum's members
      to another - which is how SNMP_GAUGE32 ended up credited to SnmpTag.
    * Preprocessor lines are stripped. Enum bodies are conditionally compiled
      (`enum class RouteType` guards members with `#if PC_ENABLE_WEBSOCKET`), so
      leaving them in harvests the FLAG NAME as if it were a member - which had
      the checker demanding `RouteType::PC_ENABLE_WEBDAV` for a `#define`.
    """
    out = {}
    for h in headers(root):
        text = _decomment(open(h, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"enum\s+class\s+(\w+)\s*(?::[^{;]*)?\{([^{}]*)\}", text):
            owner, body = m.group(1), m.group(2)
            body = re.sub(r"^\s*#.*$", "", body, flags=re.M)  # drop #if / #endif
            for mem in re.findall(r"\b([A-Z][A-Z0-9_]{2,})\b", body):
                out.setdefault(mem, set()).add(owner)
    return out


def enum_classes(root=None):
    """Just the enum-class type names."""
    names = set()
    for h in headers(root):
        text = _decomment(open(h, encoding="utf-8", errors="replace").read())
        names.update(re.findall(r"enum\s+class\s+(\w+)", text))
    return names
