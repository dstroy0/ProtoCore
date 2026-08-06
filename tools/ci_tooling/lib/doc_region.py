#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Primitives for writing generated regions into tracked documents.

Eleven generators had each reimplemented the same four steps: find the repo root,
locate a marked region, replace it, and support --check. The reimplementations did
not agree, and the disagreements were the bugs:

* Two incompatible answers to the prettier collision. prettier reformats markdown
  AFTER a generator runs, so a byte comparison in --check reports STALE forever.
  Three generators solved it by wrapping output in prettier-ignore fences (exact
  compare, still detects real drift); three others collapsed whitespace before
  comparing, which makes --check blind to the drift it exists to catch. Fences win,
  so that is what `Region` emits.

* Repo root by counting dirname levels. Nesting a script one directory deeper
  silently retargets it, which is how an asset generator once aimed above the repo
  root, and it broke six scripts again during the ci_tooling move. `repo_root()`
  searches for a marker instead, so depth stops mattering.

* CRLF handling present in nine files and absent in five.

`sub_once` is here for a different lesson: a `str.replace` that matches nothing
returns the original string, so a generator "succeeds" while shipping the stale
text it meant to fix. That happened to a README tagline, so the assertion is the
primitive, not the convention.
"""

import os
import re
import sys

# A directory is the repo root when it holds these. Both, because src/ alone also
# matches a checkout of some other library vendored inside this one.
ROOT_MARKERS = ("library.json", "src")


def repo_root(start=None):
    """Walk up from `start` (default: this file) until the root markers appear.

    Never counts dirname levels: a script that moves between directories keeps
    resolving the same root, which is the whole point.
    """
    d = os.path.dirname(os.path.abspath(start or __file__))
    while True:
        if all(os.path.exists(os.path.join(d, m)) for m in ROOT_MARKERS):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            raise SystemExit(
                f"doc_region: no repo root above {start or __file__} " f"(looking for {' + '.join(ROOT_MARKERS)})"
            )
        d = parent


def tool_id(script_path):
    """Repo-relative path of a generator, for embedding in its own markers.

    Derived rather than hardcoded so a marker cannot outlive a move: the
    ci_tooling reorg had to rewrite these strings in six documents by hand.
    """
    return os.path.relpath(os.path.abspath(script_path), repo_root(script_path)).replace(os.sep, "/")


def sub_once(text, old, new, what):
    """Replace exactly once, or fail loudly.

    A no-op replace is indistinguishable from success at the call site, and ships
    whatever stale content it was meant to replace.
    """
    if old not in text:
        raise SystemExit(f"doc_region: {what}: pattern not found verbatim; refusing to write")
    return text.replace(old, new, 1)


def re_sub_once(text, pattern, repl, what, flags=0):
    """Regex counterpart of `sub_once`: substitute exactly once, or fail.

    Generators often patch a sentence outside their region ("ships N runnable
    examples"). `re.sub(..., count=1)` returns the text unchanged when the
    surrounding wording drifts, so the number quietly stops being maintained while
    the generator still reports success.
    """
    new, n = re.subn(pattern, repl, text, count=1, flags=flags)
    if n != 1:
        raise SystemExit(f"doc_region: {what}: pattern {pattern!r} matched {n} times; " "refusing to write")
    return new


def fenced_code(md, lang=None):
    """Yield (language, code, start_line) for each fenced block in `md`.

    Needed by the checkers that read example READMEs: their annotated-source
    blocks are hand-rolled on purpose, so the code inside them has to be
    validated where it sits rather than regenerated.
    """
    for m in re.finditer(r"^([ \t]*)```([^\n`]*)\n(.*?)^\1```[ \t]*$", md, re.S | re.M):
        info = m.group(2).strip()
        if lang is None or info.split(":")[0].strip() == lang:
            yield info, m.group(3), md[: m.start()].count("\n") + 1


class Region:
    """One generated region inside one document.

    The marker text embeds `tool`, so the document always names the file that
    owns the region, and `prettier` is asked to leave the body alone so --check
    can compare bytes.
    """

    def __init__(self, doc, key, tool, prettier_ignore=True):
        self.doc = doc
        self.key = key
        self.tool = tool
        self.prettier_ignore = prettier_ignore

    @property
    def begin(self):
        return f"<!-- BEGIN GENERATED {self.key} ({self.tool}) -->"

    @property
    def end(self):
        return f"<!-- END GENERATED {self.key} -->"

    def wrap(self, body):
        body = body.strip("\n")
        if self.prettier_ignore:
            # prettier runs after every generator; without this it re-pads tables
            # and the next --check reports STALE on content that did not change.
            body = f"<!-- prettier-ignore-start -->\n\n{body}\n\n<!-- prettier-ignore-end -->"
        return f"{self.begin}\n\n{body}\n\n{self.end}"

    def replace_in(self, text, body):
        if self.begin not in text or self.end not in text:
            raise SystemExit(f"doc_region: {self.doc}: missing markers for {self.key}\n" f"  expected: {self.begin}")
        block = self.wrap(body)
        return re.sub(re.escape(self.begin) + r".*?" + re.escape(self.end), lambda _m: block, text, flags=re.S)


def apply(doc, bodies, check=False, label=None, transform=None):
    """Write every region of one document, or report staleness.

    `bodies` maps Region -> rendered body. Byte comparison, because the regions
    are prettier-ignored; CRLF is preserved from the file as read.

    `transform` is an optional callable applied to the whole document after the
    regions are replaced, for the counts and dates that generators maintain in
    prose OUTSIDE their region. Use `sub_once` / `re_sub_once` inside it so a
    reworded sentence fails loudly instead of silently going stale.
    """
    path = doc if os.path.isabs(doc) else os.path.join(repo_root(), doc)
    with open(path, "r", encoding="utf-8", newline="") as fh:
        raw = fh.read()
    nl = "\r\n" if "\r\n" in raw else "\n"
    flat = raw.replace("\r\n", "\n")

    new = flat
    for region, body in bodies.items():
        new = region.replace_in(new, body)
    if transform is not None:
        new = transform(new)

    name = label or os.path.relpath(path, repo_root())
    if check:
        if new != flat:
            print(f"STALE: {name} generated regions are out of date", file=sys.stderr)
            return 1
        print(f"{name}: generated regions up to date")
        return 0

    if new != flat:
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(new.replace("\n", nl))
        print(f"wrote {name}: {', '.join(r.key for r in bodies)}")
    else:
        print(f"{name}: unchanged")
    return 0
