#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""affected_common.py - content-aware CI change classification.

Shared by test/select_envs.py (native test envs) and tools/ci_tooling/generate/example_footprints.py
(ESP32 example builds). EVERY feature commit touches the same three "global" files -
src/protocore_config.h (a new gate), test/test_matrix.json (a new env) and the generated
platformio.ini - and a name-only diff can only treat those as "runs everything". But
a feature's change to each is *additive*: a new default-off `#define PC_ENABLE_X 0`
gate, a new `native_x` env object, a new `[env:native_x]` section. None of that alters
how any EXISTING env/example compiles, so it must not force a whole-matrix rebuild.

These helpers diff the OLD vs NEW content of those files and decide whether the change
is additive (-> select only the new env(s) / ignore) or genuinely shared (-> FULL).

Read helpers take a git ref (or None -> the working tree) so callers can classify a
push (base ref vs working tree) or any historical commit range (base ref vs head ref).
"""

import difflib
import json
import re
import subprocess


def git_show(ref, path):
    """Return the blob at <ref>:<path> as text, or None if it does not exist there."""
    try:
        out = subprocess.run(
            ["git", "show", f"{ref}:{path}"],
            capture_output=True,
            check=True,
        )
        return out.stdout.decode("utf-8", "replace")
    except (subprocess.CalledProcessError, OSError):
        return None


def file_at(ref, path):
    """Content of <path> at <ref>, or - when ref is None - from the working tree."""
    if ref is None:
        try:
            with open(path, encoding="utf-8") as fh:
                return fh.read()
        except OSError:
            return None
    return git_show(ref, path)


# --------------------------------------------------------------------------
# src/protocore_config.h - an additive gate change is inert for every existing env.
# --------------------------------------------------------------------------

_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)")
# Preprocessor directives whose insertion cannot change an existing TU's compilation
# (they only guard / default / diagnose). #include and #undef are deliberately absent.
_INERT_DIRECTIVES = {"ifndef", "ifdef", "if", "elif", "else", "endif", "error", "warning", "pragma"}


def _defined_macros(text):
    out = set()
    for line in text.splitlines():
        m = _DEFINE_RE.match(line)
        if m:
            out.add(m.group(1))
    return out


def _is_comment_or_blank(s):
    s = s.strip()
    return s == "" or s.startswith(("//", "/*", "*/", "*"))


def _added_line_inert(s, old_macros):
    """An added line is inert if it is a comment/blank, an inert preprocessor directive,
    or a `#define` of a macro that did NOT exist before (a brand-new default-off gate /
    tunable - nothing that already compiles references it)."""
    if _is_comment_or_blank(s):
        return True
    t = s.strip()
    if not t.startswith("#"):
        return False  # actual C/C++ code
    directive = t[1:].lstrip()
    kw = directive.split(None, 1)[0] if directive else ""
    if kw in _INERT_DIRECTIVES:
        return True
    if kw == "define":
        m = _DEFINE_RE.match(t)
        name = m.group(1) if m else ""
        return bool(name) and name not in old_macros
    return False  # #include, #undef, ... -> could change existing behavior


def config_header_additive(old, new):
    """True iff a src/protocore_config.h change cannot alter any existing env's compilation:
    every removed/replaced OLD line is a comment or blank, and every added/replacement NEW
    line is inert (comment/blank, an inert preprocessor directive, or a new-macro #define).

    A typical feature diff here is exactly that: a new `#ifndef PC_ENABLE_X / #define ... 0
    / #endif` block (plus its doc comment). Anything else - editing an existing #define,
    adding real code, a new #include - returns False so the caller falls back to FULL.
    """
    if old is None or new is None:
        return False
    old_lines = old.splitlines()
    new_lines = new.splitlines()
    old_macros = _defined_macros(old)
    sm = difflib.SequenceMatcher(a=old_lines, b=new_lines, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        for s in old_lines[i1:i2]:
            if not _is_comment_or_blank(s):
                return False
        for s in new_lines[j1:j2]:
            if not _added_line_inert(s, old_macros):
                return False
    return True


# --------------------------------------------------------------------------
# test/test_matrix.json - which native_* envs were added / changed.
# --------------------------------------------------------------------------


def matrix_changed_envs(old_text, new_text):
    """Set of native_* env names added or modified in test/test_matrix.json, or "FULL" if
    a shared/global part changed (any top-level key other than `envs`) or the diff cannot
    be attributed. A removed env selects nothing (its suite is gone)."""
    if old_text is None or new_text is None:
        return "FULL"
    try:
        old = json.loads(old_text)
        new = json.loads(new_text)
    except ValueError:
        return "FULL"
    for k in (set(old) | set(new)) - {"envs"}:
        if old.get(k) != new.get(k):
            return "FULL"
    oe = old.get("envs", {})
    ne = new.get("envs", {})
    changed = set()
    for name in set(oe) | set(ne):
        if oe.get(name) != ne.get(name) and name in ne:
            changed.add(name)
    return changed


# --------------------------------------------------------------------------
# platformio.ini - which [env:native_*] sections changed (comments ignored).
# --------------------------------------------------------------------------

_ENV_HDR = re.compile(r"^\[env:(native[A-Za-z0-9_]*)\]$")


def _ini_split(text):
    """{section-header -> body-lines} for every section; the preamble is "<head>". A section
    body is normalized: `;` comments and blank lines dropped (they never affect the build,
    and the generated block's `N native envs` count-comment and per-env `; desc` comments
    would otherwise show phantom changes)."""
    sections = {}
    cur = "<head>"
    buf = []

    def norm(lines):
        return [ln.rstrip() for ln in lines if ln.strip() and not ln.lstrip().startswith(";")]

    for line in text.splitlines():
        st = line.strip()
        if st.startswith("[") and st.endswith("]"):
            sections[cur] = norm(buf)
            cur = st
            buf = []
        else:
            buf.append(line)
    sections[cur] = norm(buf)
    return sections


def _ini_env_name(header):
    m = _ENV_HDR.match(header)
    name = m.group(1) if m else None
    if name == "native_base":  # the shared base env, not a feature env
        return None
    return name


def ini_changed_envs(old_text, new_text):
    """Set of native_* envs whose [env:native_*] section was added/changed in platformio.ini,
    or "FULL" if anything outside those sections changed (the head, [platformio], [env],
    [native_base], or a non-native env)."""
    if old_text is None or new_text is None:
        return "FULL"
    olds = _ini_split(old_text)
    news = _ini_split(new_text)
    changed = set()
    for h in set(olds) | set(news):
        name = _ini_env_name(h)
        if olds.get(h) == news.get(h):
            continue
        if name is None:  # a shared/head/base/non-native section changed
            return "FULL"
        if h in news:  # added or modified feature env
            changed.add(name)
    return changed
