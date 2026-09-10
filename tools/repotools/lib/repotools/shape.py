#!/usr/bin/env python3
# repotools-stamp: lib/repotools/shape.py dffd5839b6ab3b7f
# repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""Reducing a file to its shape, so two copies of one tool match after they have drifted apart.

An exact hash finds nothing across repositories. The eight copies of `codemask.py` on this machine
differ in the header line, in a project name inside a docstring, in where a formatter broke a line,
and in one identifier. Every one of those is invisible to the reader and fatal to a byte comparison.

    from repotools import shape

    shape.of(path)          # ShapeOf(exact, shaped, tokens, lines)

`exact` is the SHA-256 of the file with line endings normalized. `shaped` is the hash of what
remains after four reductions:

  1. Comments and docstrings come off. The measured drift is almost entirely prose: one copy said
     "rather than" where a later sweep had written "instead of".
  2. String literals fold to one token. A project name inside a message is that project's, and the
     code around it is everybody's.
  3. Numbers keep their value. A bound is part of the algorithm and two tools that differ in one are
     two tools.
  4. Identifiers renumber by first appearance, so `loculus` and `slot` reach the same token.

WHAT THIS DELIBERATELY DOES NOT DO

It does not parse. A tokenizer that fails on a file yields that file's exact hash and an empty shape,
and the caller reports it as unshaped instead of dropping it. A survey that silently skips what it
cannot read is the same defect as a prose root that no longer exists.

SAME SHAPE NEVER MEANS SAME BEHAVIOR. THIS IS THE INSTRUMENT'S LIMIT.

Identifiers renumber by first appearance, which is what lets two copies match after they drift. It
also means a misspelled identifier is invisible here. Measured on this machine: ProtoCore's
`move_code.py` reads `args.anchor_before` and MMgr's reads `args.ancorae_before`, so the MMgr copy
raises AttributeError on every anchored move. Both hash to cb2b60bdb26d6c01. One works, one is
broken, and this reports them as one shape held twice.

A second way it misleads: two generations of a tool that share a command surface and an opening
docstring land in one bucket while sharing no API at all. Three trees held a `readclean.py` that way,
and the test promoted with one of them could not import against another.

So a group here is a claim that two files are the same SHAPE. Deciding which text survives a
promotion means reading both in full. A survey narrows a thousand files to a list a person can read;
the reading is the part it does not do, and skipping it promoted a broken tool.
"""

import hashlib
import io
import os
import re
import tokenize as pytokenize

C_LIKE = (".c", ".h", ".cpp", ".hpp", ".cc", ".cu", ".cuh", ".js", ".cjs")
PY_LIKE = (".py",)
SHELL_LIKE = (".sh", ".bash", ".ps1")

# Structure never renames. `while` colliding with `for` would put every loop in one bucket.
C_KEYWORDS = set(
    """auto break case char const continue default do double else enum extern float for goto if
    inline int long register restrict return short signed sizeof static struct switch typedef union
    unsigned void volatile while _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary
    _Noreturn _Static_assert _Thread_local static_assert alignas alignof bool true false NULL
    defined include define ifndef ifdef endif elif pragma error undef line""".split()
)

PY_KEYWORDS = set(
    """False None True and as assert async await break class continue def del elif else except
    finally for from global if import in is lambda nonlocal not or pass raise return try while with
    yield match case self""".split()
)

C_TOKEN = re.compile(
    r"""
      (?P<id>[A-Za-z_][A-Za-z_0-9]*)
    | (?P<num>0[xX][0-9A-Fa-f]+[uUlL]*|\d+\.?\d*([eE][-+]?\d+)?[uUlLfF]*)
    | (?P<str>"(\\.|[^"\\])*"|'(\\.|[^'\\])*')
    | (?P<op><<=|>>=|\.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!<>=]=|\#\#|[-+*/%&|^~!<>=?:;,.(){}\[\]\#])
    """,
    re.VERBOSE,
)


class ShapeOf:
    """One file reduced. `shaped` is empty where the file could not be tokenized."""

    def __init__(self, path, exact, shaped, tokens, lines):
        self.path = path
        self.exact = exact
        self.shaped = shaped
        self.tokens = tokens
        self.lines = lines

    def unshaped(self):
        return not self.shaped


def _digest(parts):
    handle = hashlib.sha256()
    for one in parts:
        handle.update(one.encode("utf-8", "replace"))
        handle.update(b"\x00")
    return handle.hexdigest()[:16]


def _canon(pairs, keywords):
    """Renumber identifiers by first appearance, keeping keywords and folding literals.

    `pairs` is `(kind, text)`. A name reused later maps to the number it was first given, so a shape
    cannot be reached by accident from a different one.
    """
    seen = {}
    out = []
    for kind, text in pairs:
        if kind == "id":
            if text in keywords:
                out.append(text)
            else:
                out.append("I%d" % seen.setdefault(text, len(seen)))
        elif kind == "str":
            out.append("S")
        else:
            out.append(text)
    return out


def _c_pairs(text):
    """C family tokens, comments removed by the tokenizer never matching them.

    The regex has no comment alternative, so a `//` line yields its operator tokens. Comments are
    blanked before this runs.
    """
    out = []
    for found in C_TOKEN.finditer(text):
        kind = found.lastgroup
        if kind in ("id", "num", "str", "op"):
            out.append((kind, found.group()))
    return out


def _blank_c_comments(text):
    """Every comment byte becomes a space, with literals and newlines left where they are.

    Blanked in place instead of deleted, because a caller counting lines needs the count to hold. A
    regex over `//.*$` truncates `http://x` inside a literal, so the scan tracks quotes.
    """
    out = []
    at, size = 0, len(text)
    while at < size:
        char = text[at]
        if char in "\"'":
            quote = char
            out.append(char)
            at += 1
            while at < size:
                if text[at] == "\\":
                    out.append(text[at : at + 2])
                    at += 2
                    continue
                out.append(text[at])
                if text[at] == quote:
                    at += 1
                    break
                at += 1
            continue
        if char == "/" and at + 1 < size and text[at + 1] == "/":
            while at < size and text[at] != "\n":
                out.append(" ")
                at += 1
            continue
        if char == "/" and at + 1 < size and text[at + 1] == "*":
            while at < size and not (text[at] == "*" and at + 1 < size and text[at + 1] == "/"):
                out.append("\n" if text[at] == "\n" else " ")
                at += 1
            out.append("  ")
            at += 2
            continue
        out.append(char)
        at += 1
    return "".join(out)


def _py_pairs(text):
    """Python tokens with comments, docstrings and layout removed.

    Uses the standard library tokenizer, so an f-string, a nested quote and a line continuation are
    handled by the same code the interpreter uses. A file that fails to tokenize returns None, and
    the caller reports it instead of treating it as empty.
    """
    out = []
    try:
        stream = pytokenize.generate_tokens(io.StringIO(text).readline)
        prev = None
        for kind, value, _start, _end, _line in stream:
            if kind in (pytokenize.COMMENT, pytokenize.NL, pytokenize.NEWLINE, pytokenize.ENCODING):
                continue
            if kind in (pytokenize.INDENT, pytokenize.DEDENT, pytokenize.ENDMARKER):
                continue
            if kind == pytokenize.STRING:
                # A string alone on a logical line is a docstring. Everything before it on the line
                # would be an operator or a name, so the previous token settles it.
                if prev is None or prev in (":", None):
                    prev = value
                    continue
                out.append(("str", value))
            elif kind == pytokenize.NAME:
                out.append(("id", value))
            elif kind == pytokenize.NUMBER:
                out.append(("num", value))
            else:
                out.append(("op", value))
            prev = value
        return out
    except (pytokenize.TokenError, IndentationError, SyntaxError):
        return None


def _shell_pairs(text):
    """Shell reduced to its non-comment words.

    A shell script has no tokenizer worth writing here, so a comment is a `#` outside quotes and the
    rest is split on whitespace. Coarse, and enough to match two copies of one hook.
    """
    out = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        for word in stripped.split():
            kind = "num" if word.isdigit() else ("id" if re.match(r"^[A-Za-z_]\w*$", word) else "op")
            out.append((kind, word))
    return out


def of(path):
    """The ShapeOf for one file, chosen by extension."""
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    normalized = text.replace("\r\n", "\n")
    exact = _digest([normalized])
    lines = normalized.count("\n") + 1
    extension = os.path.splitext(path)[1].lower()

    if extension in PY_LIKE:
        pairs = _py_pairs(normalized)
        keywords = PY_KEYWORDS
    elif extension in C_LIKE:
        pairs = _c_pairs(_blank_c_comments(normalized))
        keywords = C_KEYWORDS
    elif extension in SHELL_LIKE:
        pairs = _shell_pairs(normalized)
        keywords = set()
    else:
        pairs = None
        keywords = set()

    if pairs is None:
        return ShapeOf(path, exact, "", 0, lines)
    canonical = _canon(pairs, keywords)
    return ShapeOf(path, exact, _digest(canonical), len(canonical), lines)
