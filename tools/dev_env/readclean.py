#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Three reading passes over a ProtoCore module. Writes nothing.

  code <module.h> ...     comments stripped: the structure, with nothing to take on trust
  blind <module.h> ...    comments stripped AND every name this project chose replaced, so the code
                          is read for what it DOES rather than for what it is called
  claims <module.h> ...   every comment paired with the code it sits above, so the prose can be
                          checked against what the code does instead of read as if it were true

WHY BLIND. A name is a claim, and it is the one claim that never gets checked: a function called
`verify` is read as verifying, an `ok` member is read as an outcome, a `_reset` entry is assumed to
clear something. With those names replaced, only what the statements actually do is left. Anything
that then looks wrong IS wrong, rather than being a mismatch the reader was primed not to see.

WHAT BLIND KEEPS, AND WHY IT IS NOT A GENERIC RENAMER. This tree has one shape, and the shape is
grammar rather than vocabulary: an entry takes `uint8_t *restrict work` and nothing else, operands
are members of `<X>V`, state is carved out of `PROTOCORE_<X>_BORROW` at `<X>_OFF_*` offsets under a
static_assert. Blinding that away would leave text no one can check conformity against. So the
suffixes and the fixed vocabulary survive and only the IDENTITY moves:

    Sha256Ns Sha256Vars Sha256V Sha256Ctx  ->  X1Ns X1Vars X1V X1Ctx
    PROTOCORE_SHA256_BORROW SHA256_OFF_W   ->  PROTOCORE_X1_BORROW X1_OFF_A
    protocore_sha256_update                ->  protocore_x1_fn3
    work, proto_bool, PROTOCORE_BEGIN_DECLS, uint8_t, static_assert   ->  unchanged

What is left reads as "an entry, taking the borrow, casting a region at an asserted offset" with no
opinion about whether that region is a hash state or a parser. That is the question worth asking.

Run `blind` FIRST, form a judgement, then `code` or `claims` to put the names back. The legend is
written to a file and never printed, because reading it in the same breath undoes the pass.

Comment removal uses strip_comments.rewrite - the literal-aware one already in the tree - not a
regex: `//.*$` truncates "http://x", and a non-greedy /*...*/ eats a "/*" inside a string literal.

Usage:
    python tools/dev_env/readclean.py code   PATH [PATH ...]
    python tools/dev_env/readclean.py blind  PATH [PATH ...] [--legend FILE] [--keep NAME,...]
    python tools/dev_env/readclean.py claims PATH [PATH ...]

A PATH is a module header (its .c is read with it), a single file, or a directory.
"""

import io, os, re, sys

ROOT = os.getcwd()
sys.path.insert(0, os.path.join(ROOT, "tools", "dev_env"))
from strip_comments import rewrite
from codemask import code_mask

# C grammar. Blinding a keyword turns the code into noise instead of into structure.
KEYWORDS = {
    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    "_Alignof",
    "_Alignas",
    "_Static_assert",
    "_Bool",
    "static_assert",
    "alignof",
    "bool",
    "true",
    "false",
    "NULL",
    "nullptr",
    "asm",
    "__asm__",
    "__attribute__",
    "__inline__",
    "__restrict",
    "typeof",
    "_Generic",
    "_Noreturn",
    "_Thread_local",
}
# Shared vocabulary this codebase did not choose.
STDLIB = {
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "size_t",
    "ssize_t",
    "ptrdiff_t",
    "intptr_t",
    "uintptr_t",
    "uintmax_t",
    "intmax_t",
    "wchar_t",
    "va_list",
    "va_start",
    "va_end",
    "va_arg",
    "va_copy",
    "FILE",
    "time_t",
    "clock_t",
    "memcpy",
    "memmove",
    "memset",
    "memcmp",
    "memchr",
    "strlen",
    "strcmp",
    "strncmp",
    "strcpy",
    "strncpy",
    "strcat",
    "strncat",
    "strchr",
    "strrchr",
    "strstr",
    "strtol",
    "strtoul",
    "strtoull",
    "snprintf",
    "vsnprintf",
    "sprintf",
    "printf",
    "fprintf",
    "puts",
    "putchar",
    "abort",
    "exit",
    "abs",
    "labs",
    "offsetof",
    "stderr",
    "stdout",
    "stdin",
    "isdigit",
    "isalpha",
    "isalnum",
    "isspace",
    "isupper",
    "islower",
    "toupper",
    "tolower",
    "INT_MAX",
    "INT_MIN",
    "UINT_MAX",
    "SIZE_MAX",
    "CHAR_BIT",
    "EOF",
}
# ProtoCore's own grammar: the fixed words the shape is written in. `work` is the borrow every entry
# takes, `ok` is where every entry states its outcome, and the DECLS macros bracket every header.
# These say nothing about which module is being read, so blinding them costs the reader the shape
# and buys no independence.
SHAPE = {
    "work",
    "ok",
    "proto_bool",
    "proto_u32",
    "proto_u16",
    "proto_u8",
    "proto_i32",
    "proto_i16",
    "PROTO_TRUE",
    "PROTO_FALSE",
    "PROTOCORE_BEGIN_DECLS",
    "PROTOCORE_END_DECLS",
    "PROTOCORE_ARENA_ALIGN",
    "PROTOCORE_ARENA_MAX_ALIGN",
    "protocore_config",
}
# An __attribute__ argument is grammar too. `__attribute__((unused))` came out as
# `__attribute__((v7))`, which reads as a call on a variable rather than as the attribute that lets
# an unreferenced table drop.
ATTRS = {
    "unused",
    "used",
    "aligned",
    "packed",
    "always_inline",
    "noreturn",
    "deprecated",
    "fallthrough",
    "weak",
    "section",
    "visibility",
    "warn_unused_result",
    "nonnull",
    "pure",
    "const_",
    "hot",
    "cold",
    "constructor",
    "destructor",
    "format",
    "may_alias",
    "cleanup",
    "transparent_union",
}
SAFE = KEYWORDS | STDLIB | SHAPE | ATTRS

IDENT = re.compile(r"\b[A-Za-z_]\w*\b")
# The shape suffixes, longest first so `Vars` is tested before the bare object name.
ROLE_SUFFIX = ("Vars", "Args", "Ctx", "Ns", "V")
# The one member whose type IS the shape: an entry takes the borrow and nothing else.
ENTRY_MEMBER = re.compile(r"\(\s*\*\s*const\s+(\w+)\s*\)\s*\(\s*uint8_t\s*\*\s*restrict\s+work\s*\)")
# A function-like macro and its body: the cast that reads a region off the borrow.
REGION_MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\([^)]*\)[ \t]*(.*)$", re.M)


def paths(rel):
    """A module header pairs with its .c; anything else (a test .c, a suite dir) stands alone."""
    p = os.path.join(ROOT, rel.replace("/", os.sep))
    if os.path.isdir(p):
        return sorted(os.path.join(p, n) for n in os.listdir(p) if n.endswith((".c", ".h")) and n != "unity_runner.c")
    if rel.endswith(".h"):
        return [x for x in (p, p[:-2] + ".c") if os.path.exists(x)]
    return [p] if os.path.exists(p) else []


def banner(p):
    rel = os.path.relpath(p, ROOT).replace("\\", "/")
    return "\n" + "=" * 90 + "\n### " + rel + "\n" + "=" * 90


def stripped(p):
    """The file with every comment gone, the licence and @file block included.

    Not a formality: a doc block states what the code is MEANT to do, and reading it first is how a
    conformity pass ends up confirming the prose instead of the code.
    """
    text = io.open(p, encoding="utf-8", errors="replace").read()
    return "\n".join(ln.rstrip() for ln in rewrite(text, False).splitlines() if ln.strip())


class Blinder(object):
    """One naming table for a whole run, so a name crossing from the .h into the .c stays itself.

    The categories are the shape's own, not a compiler's. Nothing here parses C: the pass has to be
    CONSISTENT, not correct, because its job is to remove meaning rather than to recover it. A name
    filed under the wrong category still reads as a name with nothing to assume about it.
    """

    def __init__(self, keep):
        self.keep = keep
        self.table = {}
        self.n = {}

    def _next(self, kind):
        self.n[kind] = self.n.get(kind, 0) + 1
        return "%s%d" % (kind, self.n[kind])

    def _object(self, stem):
        """The generic identity for a module's object stem, shared by all of its role types.

        Sha256Ns, Sha256Vars, Sha256V and Sha256Ctx are four spellings of ONE module, and giving
        them four unrelated generic names would hide the very relationship the shape is built on.
        """
        key = ("obj", stem)
        if key not in self.table:
            self.table[key] = self._next("X")
        return self.table[key]

    def rename(self, name):
        if name in SAFE or name in self.keep:
            return name
        if name in self.table:
            return self.table[name]

        out = None
        # A role type: keep the suffix, blind the stem, and keep every role of one module together.
        for suf in ROLE_SUFFIX:
            if name.endswith(suf) and len(name) > len(suf) and name[0].isupper():
                out = self._object(name[: -len(suf)]) + suf
                break

        if out is None:
            m = re.match(r"^PROTOCORE_(\w+)_BORROW$", name)
            if m:
                out = "PROTOCORE_%s_BORROW" % self._object(m.group(1))
        if out is None:
            m = re.match(r"^PROTOCORE_ENABLE_(\w+)$", name)
            if m:
                out = "PROTOCORE_ENABLE_%s" % self._object(m.group(1))
        if out is None:
            m = re.match(r"^PROTOCORE_(\w+)_H$", name)
            if m:
                out = "PROTOCORE_%s_H" % self._object(m.group(1))
        if out is None:
            # A carved region: `SHA256_OFF_W` and `SHA256_CTX(w)` are the offset and the cast that
            # reads it. The module stem is blinded; which region it is becomes a letter, because
            # "the second region" is the only thing about it a reader should be trusting.
            m = re.match(r"^([A-Z][A-Z0-9]*(?:_[A-Z0-9]+)*?)_OFF_([A-Z0-9_]+)$", name)
            if m:
                out = "%s_OFF_%s" % (self._object(m.group(1)), self._region(m.group(1), m.group(2)))
        if out is None and name.isupper() and "_" in name:
            # The cast that reads a region, beside the offset that locates it. `SHA256_CTX(w)` and
            # `SHA256_OFF_CTX` name the same region, so they take the same letter - `X1_A(w)` at
            # `X1_OFF_A`. Falling through to the generic macro bucket gave them unrelated numbers
            # and hid the one thing worth checking: that each cast reads the offset it belongs to.
            for cut in range(len(name) - 1, 0, -1):
                if name[cut] != "_":
                    continue
                stem, rest = name[:cut], name[cut + 1 :]
                if ("obj", stem) in self.table and rest:
                    out = "%s_%s" % (self.table[("obj", stem)], self._region(stem, rest))
                    break
        if out is None:
            m = re.match(r"^protocore_(\w+)$", name)
            if m:
                out = "protocore_" + self._next("fn")
        if out is None and ("obj", name) in self.table:
            # The bare object: `Sha256` beside `Sha256Ns` and `Sha256V`. It carries no suffix, so it
            # reached the generic bucket and came out as T1 while its own role types were X5 - the
            # published table looked unrelated to the namespace it is an instance of.
            out = self.table[("obj", name)]
        if out is None:
            if name.isupper():
                out = self._next("M")
            elif name[0].isupper():
                out = self._next("T")
            else:
                out = self._next("v")

        self.table[name] = out
        return out

    def _region(self, stem, region):
        key = ("region", stem)
        seen = self.table.setdefault(key, {})
        if region not in seen:
            seen[region] = chr(ord("A") + len(seen))
        return seen[region]

    def whole(self, text):
        """`text` blinded in one pass, so a literal and a directive can be told from a name.

        Not line by line. Three things in a C file look like identifiers and are not:

          - the bytes inside a STRING LITERAL. Every static_assert message came out as
            `"PROTOCORE_X1_BORROW v10 short v11 v12"` - the diagnostic a build would print,
            rewritten into nonsense. code_mask already marks a literal as non-code, so it is asked.
          - a DIRECTIVE keyword. `#ifndef PROTOCORE_SHA256_H` became `#v1 PROTOCORE_X1_H`, losing
            the guard, the gate and every conditional arm.
          - an #include PATH. It is a location, not a claim about behaviour, and blinding it leaves
            a file nothing can place in the tree.
        """
        mask = code_mask(text)
        skip = []
        for m in re.finditer(r"^[ \t]*#[ \t]*include\b.*$", text, re.M):
            skip.append((m.start(), m.end()))
        for m in re.finditer(r"^[ \t]*#[ \t]*\w+", text, re.M):
            skip.append((m.start(), m.end()))

        def protected(i):
            return any(a <= i < b for a, b in skip)

        # An ENTRY is not a variable: it is the one member whose type IS the shape's signature. It
        # is registered ahead of the general pass, or it falls through and reads as another local.
        for e in ENTRY_MEMBER.finditer(text):
            nm = e.group(1)
            if mask[e.start()] and nm not in self.table and nm not in SAFE and nm not in self.keep:
                self.table[nm] = self._next("e")

        # A REGION MACRO is named for its region by its BODY, not by its own spelling: sha256.c
        # writes `SHA256_FS(w)` over `SHA256_OFF_STATE`. Reading the letter off the macro's suffix
        # filed FS as a region of its own, so the cast and the offset it reads came out as X1_C and
        # X1_OFF_B - and whether each cast reads the offset it belongs to is the whole question.
        for d in REGION_MACRO.finditer(text):
            nm, body = d.group(1), d.group(2)
            if not mask[d.start()] or nm in self.table or nm in SAFE or nm in self.keep:
                continue
            off = re.search(r"\b([A-Z][A-Z0-9_]*?)_OFF_([A-Z0-9_]+)\b", body)
            if off:
                self.table[nm] = "%s_%s" % (self._object(off.group(1)), self._region(off.group(1), off.group(2)))

        out, at = [], 0
        for m in IDENT.finditer(text):
            if not mask[m.start()] or protected(m.start()):
                continue
            out.append(text[at : m.start()])
            out.append(self.rename(m.group(0)))
            at = m.end()
        out.append(text[at:])
        return "".join(out)

    def legend(self):
        return sorted((v, k) for k, v in self.table.items() if isinstance(k, str) and isinstance(v, str))


def show_code(p, _state):
    print(banner(p))
    print(stripped(p))


def show_blind(p, state):
    """The structure with this project's names taken off it."""
    print(banner(p))
    print(state["blinder"].whole(stripped(p)))


def show_claims(p, _state):
    """Every comment, with the code line it introduces, so a claim can be met with its subject."""
    text = io.open(p, encoding="utf-8", errors="replace").read()
    m = code_mask(text)
    lines = text.splitlines()
    print(banner(p))
    i, n = 0, len(text)
    while i < n:
        if m[i] or text[i] in " \t\r\n" or text[i] in "\"'":
            i += 1
            continue
        j = i
        while j < n and not m[j]:
            j += 1
        block = text[i:j].strip()
        if block.startswith(("//", "/*")) and len(block) > 4:
            ln = text.count("\n", 0, i) + 1
            end = text.count("\n", 0, j) + 1
            subject = ""
            for k in range(end - 1, min(end + 3, len(lines))):
                if k < len(lines) and lines[k].strip():
                    subject = lines[k].strip()
                    break
            print("\n[%d] %s" % (ln, block))
            if subject:
                print("     -> %s" % subject)
        i = j


def main():
    args = sys.argv[1:]
    mode = args[0] if args and args[0] in ("code", "blind", "claims") else "code"
    legend, keep, rels = None, set(), []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("code", "blind", "claims"):
            pass
        elif a == "--legend":
            i += 1
            legend = args[i]
        elif a == "--keep":
            i += 1
            keep = {x.strip() for x in args[i].split(",") if x.strip()}
        elif a.startswith("-"):
            print("unknown flag %s" % a, file=sys.stderr)
            return 2
        else:
            rels.append(a)
        i += 1
    if not rels:
        print(__doc__)
        return 2

    state = {"blinder": Blinder(keep)}
    show = {"code": show_code, "blind": show_blind, "claims": show_claims}[mode]
    seen = 0
    for rel in rels:
        for p in paths(rel):
            show(p, state)
            seen += 1
    if not seen:
        print("nothing to read at: %s" % ", ".join(rels), file=sys.stderr)
        return 1

    if mode == "blind":
        pairs = state["blinder"].legend()
        # Written to a FILE and never printed. Reading it in the same breath as the blinded code
        # puts every name back before a judgement has been formed, which is the one thing this pass
        # exists to prevent.
        if legend:
            with io.open(legend, "w", encoding="utf-8", newline="") as f:
                for generic, real in pairs:
                    f.write("%-12s %s\n" % (generic, real))
            print("\n[legend: %d names -> %s. Read it AFTER you have formed a judgement.]" % (len(pairs), legend))
        else:
            print("\n[%d names blinded. No legend written: pass --legend FILE if you need one.]" % len(pairs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
