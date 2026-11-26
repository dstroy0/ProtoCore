#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Writes the primitive defaults of the classic ESP32 profile, in vendor/board_profiles/esp/esp32_defaults.h.

The classic profile turns each feature off. A primitive, such as a hash, a cipher or Base64, is on exactly when a
feature that uses it is on. This tool finds those features in src/ and writes one default for each primitive:

    #ifndef PROTOCORE_ENABLE_SHA1
    #define PROTOCORE_ENABLE_SHA1 (PROTOCORE_ENABLE_WEBSOCKET || PROTOCORE_ENABLE_TOTP || ...)
    #endif

A use is a token in src/ that equals a name that a header of the primitive's module defines: a macro, a type, a
function, an extern object or a dispatch table. Its condition is the GATE of the module that holds the token,
with each #if, #ifdef, #ifndef, #elif and #else active at its line. A condition that another condition contains
adds nothing to the OR, so it goes. A primitive with no use defaults to 0.

The macro expands where a source tests it, after each feature has its value, so the order of the headers does
not matter. A primitive that another primitive uses appears in that one's OR, so the chain resolves by expansion.

    python -m tools.ci_tooling.generate.gen_profile_primitives          # rewrite the region
    python -m tools.ci_tooling.generate.gen_profile_primitives --check  # fail when the region is stale
"""

import os
import re
import sys

from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
sys.path.insert(0, os.path.join(ROOT, "tools", "ci_tooling", "build"))
import gen_modules  # noqa: E402

PROFILE = os.path.join(ROOT, "vendor", "board_profiles", "esp", "esp32_defaults.h")
TOOL = "tools/ci_tooling/generate/gen_profile_primitives.py"
BEGIN = "// BEGIN GENERATED primitive_defaults (%s)" % TOOL
END = "// END GENERATED primitive_defaults"

# The primitives that protocore_config.h turns on and that this profile derives, in the order they print.
PRIMITIVES = (
    "SHA1",
    "SHA256",
    "SHA512",
    "MD",
    "HMAC_SHA256",
    "HMAC_SHA512",
    "POLY1305",
    "AES_CMAC",
    "GHASH",
    "CT_EQ",
    "RAWMEMCPY",
    "SSH_RSA",
    "BASE64",
    "KDF",
    "AESGCM",
    "CHACHAPOLY",
    "CHACHA20",
    "AES256CTR",
    "AES_BLOCK",
    "AES_SBOX",
    "BIGNUM",
    "CURVE25519",
    "FE25519",
    "ECDSA",
    "ED25519",
    "RSA",
    "RNG",
)
FLAGS = tuple("PROTOCORE_ENABLE_" + p for p in PRIMITIVES)

COND = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)")
TYPEDEF_END = re.compile(r"^\}\s*(\w+)\s*;")
FUNC = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(")
EXTERN = re.compile(r"^\s*extern\s+(?:const\s+)?\w+\s+\**\s*(\w+)\s*[;\[]")
TABLE = re.compile(r"^\s*(?:PROTOCORE_NS\s+|static\s+)?(?:const\s+)?\w+Ns\s+(\w+)\b")
TOKEN = re.compile(r"\b[A-Za-z_]\w*\b")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
GUARD = re.compile(r"^!?\s*defined\s*\(?\s*\w+_H\s*\)?$|^\w+_H$")
KEYWORDS = {
    "static",
    "void",
    "inline",
    "const",
    "return",
    "sizeof",
    "typedef",
    "struct",
    "__attribute__",
    "static_assert",
    "_Static_assert",
    "defined",
}


def gate_expr(gate):
    """A declared GATE as a C expression. CMake's AND, OR and NOT become C, and OPT and its level go."""
    words = (gate or "").split()
    if "OPT" in words:
        words = words[: words.index("OPT")]
    parts = [{"AND": "&&", "OR": "||", "NOT": "!"}.get(w, w) for w in words]
    if not parts:
        return ""
    return "(%s)" % " ".join(parts) if len(parts) > 1 else parts[0]


def condition_text(kind, rest):
    rest = rest.strip()
    if kind == "ifdef":
        return "defined(%s)" % rest
    if kind == "ifndef":
        return "!defined(%s)" % rest
    return "(%s)" % rest


def strip_parens(term):
    term = term.strip()
    while term.startswith("(") and term.endswith(")"):
        depth = 0
        for k, ch in enumerate(term):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0 and k < len(term) - 1:
                return term
        term = term[1:-1].strip()
    return term


def split_and(cond):
    """The top-level terms of a conjunction, each without its outer parentheses, with no repeat."""
    out, depth, start = [], 0, 0
    for k, ch in enumerate(cond):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0 and cond.startswith("&&", k):
            out.append(cond[start:k])
            start = k + 2
    out.append(cond[start:])
    terms = []
    for t in out:
        t = strip_parens(t)
        inner = split_and(t) if t != cond.strip() and "&&" in t else [t]
        for one in inner:
            if one and one not in terms:
                terms.append(one)
    return terms


def grouped(term):
    """The term in parentheses when it holds an operator, so that it keeps its meaning inside && and ||."""
    return "(%s)" % term if re.search(r"\|\||&&|\?", term) else term


def primitive_names(decl):
    """For each primitive flag, the names that the headers of its modules define, and the directories of those."""
    names, dirs = {f: set() for f in FLAGS}, {}
    for d in decl.values():
        words = (d.get("gate") or "").split()
        if len(words) != 1 or words[0] not in names:
            continue
        dirs[d["dir"]] = words[0]
        for h in sorted(os.listdir(os.path.join(ROOT, d["dir"]))):
            if not h.endswith(".h"):
                continue
            with open(os.path.join(ROOT, d["dir"], h), encoding="utf-8", errors="replace") as fh:
                text = COMMENT.sub(" ", fh.read())
            for line in text.split("\n"):
                for rx in (DEFINE, TYPEDEF_END, FUNC):
                    m = rx.match(line)
                    if m and len(m.group(1)) > 3 and not m.group(1).endswith("_H"):
                        if not m.group(1).startswith("EMBED_") and m.group(1) not in KEYWORDS:
                            names[words[0]].add(m.group(1))
                for rx in (EXTERN, TABLE):
                    m = rx.match(line)
                    if m:
                        names[words[0]].add(m.group(1))
    return names, dirs


def uses(decl, names, dirs):
    """For each primitive flag, each condition under which some other module uses one of its names."""
    owner = {}
    for flag, ns in names.items():
        for n in ns:
            owner.setdefault(n, set()).add(flag)
    dir_gate = {d["dir"]: d.get("gate") or "" for d in decl.values()}
    found = {f: [] for f in FLAGS}
    for here, _subdirs, files in os.walk(os.path.join(ROOT, "src")):
        rel_dir = os.path.relpath(here, ROOT).replace(os.sep, "/")
        own = dirs.get(rel_dir)
        module_gate = gate_expr(dir_gate.get(rel_dir, ""))
        for name in sorted(files):
            if not name.endswith((".c", ".h")):
                continue
            with open(os.path.join(here, name), encoding="utf-8", errors="replace") as fh:
                raw = fh.read()
            text = STRING.sub('""', COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), raw))
            stack = []
            for line in text.split("\n"):
                c = COND.match(line)
                if c:
                    kind, rest = c.group(1), c.group(2).strip()
                    if kind in ("if", "ifdef", "ifndef"):
                        stack.append(condition_text(kind, rest))
                    elif kind == "elif" and stack:
                        stack[-1] = "!%s && (%s)" % (stack[-1], rest)
                    elif kind == "else" and stack:
                        stack[-1] = "!%s" % stack[-1]
                    elif kind == "endif" and stack:
                        stack.pop()
                    continue
                if line.lstrip().startswith("#"):
                    continue
                for tok in set(TOKEN.findall(line)):
                    for flag in owner.get(tok, ()):
                        if flag == own:
                            continue
                        parts = ([module_gate] if module_gate else []) + [
                            s for s in stack if not GUARD.match(strip_parens(s))
                        ]
                        terms = [t for t in split_and(" && ".join(parts)) if t != flag] if parts else []
                        if terms not in found[flag]:
                            found[flag].append(terms)
    return found


def split_or(term):
    """The top-level disjuncts of a term, each without its outer parentheses."""
    out, depth, start = [], 0, 0
    for k, ch in enumerate(term):
        depth += ch == "("
        depth -= ch == ")"
        if depth == 0 and term.startswith("||", k):
            out.append(strip_parens(term[start:k]))
            start = k + 2
    out.append(strip_parens(term[start:]))
    return [t for t in out if t]


def reduce(alternatives):
    """The OR of the alternatives, with each alternative that a shorter one contains taken out.

    An alternative of one term that is an OR gives one alternative for each of its parts.
    """
    flat = []
    for a in alternatives:
        if len(a) == 1 and len(split_or(a[0])) > 1:
            flat.extend([split_and(p) for p in split_or(a[0])])
        else:
            flat.append(a)
    sets = [frozenset(a) for a in flat]
    if any(not s for s in sets):
        return ["1"]
    keep = []
    for s in sorted(set(sets), key=lambda x: (len(x), sorted(x))):
        if not any(k <= s for k in keep):
            keep.append(s)
    out = []
    for s in keep:
        terms = sorted(s)
        out.append(grouped(terms[0]) if len(terms) == 1 else "(%s)" % " && ".join(grouped(t) for t in terms))
    return sorted(out)


def render(found):
    lines = [BEGIN, "// clang-format off"]
    for flag in FLAGS:
        alts = reduce(found[flag]) if found[flag] else []
        lines.append("#ifndef %s" % flag)
        if not alts:
            lines.append("#define %s 0" % flag)
        else:
            head = "#define %s (" % flag
            row, rows = head, []
            for k, alt in enumerate(alts):
                piece = alt + (" || " if k < len(alts) - 1 else ")")
                if len(row) + len(piece) > 116:
                    rows.append(row.rstrip() + " \\")
                    row = "    "
                row += piece
            rows.append(row)
            lines.extend(rows)
        lines.append("#endif")
    lines += ["// clang-format on", END]
    return "\n".join(lines)


def main(argv):
    decl = gen_modules.declared()
    names, dirs = primitive_names(decl)
    block = render(uses(decl, names, dirs))
    with open(PROFILE, encoding="utf-8", newline="") as fh:
        text = fh.read()
    if BEGIN not in text or END not in text:
        raise SystemExit("gen_profile_primitives: %s holds no %s region" % (PROFILE, "primitive_defaults"))
    new = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END), lambda _m: block, text, flags=re.S)
    name = os.path.relpath(PROFILE, ROOT).replace(os.sep, "/")
    if "--check" in argv:
        if new != text:
            print("STALE: %s primitive defaults are out of date" % name, file=sys.stderr)
            return 1
        print("%s: primitive defaults up to date" % name)
        return 0
    if new != text:
        with open(PROFILE, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(new)
        print("wrote %s: primitive defaults" % name)
    else:
        print("%s: unchanged" % name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
