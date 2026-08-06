#!/usr/bin/env python3
r"""Fail if a ``src/`` C/C++ file uses a construct banned by ``docs/SRCBANNED.md``.

This is the mechanical guardrail behind that checklist: the pre-commit hook runs it on the staged
``src/`` sources and refuses the commit if any banned construct appears, so a violation can never
land. It scans for the machine-detectable hard bans - unbounded ``strlen``, ``<stdlib.h>`` and its
heap / parse functions, the ``auto`` keyword, blocking ``delay()``, the non-reentrant ``gmtime`` /
``localtime`` / ``ctime`` / ``asctime`` family, em-dashes, runtime dispatch that is not resolvable
from the binary (ban #22: ``virtual``, class hierarchies, RTTI, ``std::function``), the conditional
expression (ban #23: ``?:`` anywhere in a code body), and mid-file
``#include`` (ban #17: any
``#include`` after code - every include must be hoisted to the top of the file; the sole exemption is a
justified ``// PC_ALLOW_LATE_INCLUDE:`` on an ordered include that derives from earlier macros).

Ban #5 (bare ``millis()``) is deliberately *not* enforced here: it is a "for new timing" rule, so a
whole-file scan cannot distinguish a new call from a grandfathered timing site, and the clock source
(``services/clock.h``) must call the platform ``millis()`` to provide ``pc_millis()``. It stays a
review item (``rg -n '\bmillis\s*\(' src/``) rather than a mechanical gate.

Comments and string / char literals are blanked out first (line numbers preserved), so a construct
merely *named* in a comment ("without <string.h> / strlen") is not a violation - only real code is.

Usage::

    python -m tools.ci_tooling.check.check_src_banned <file>...   # scan the given files (pre-commit: the staged set)
    python -m tools.ci_tooling.check.check_src_banned --all        # scan every C/C++ file under src/

Exit status is 1 if any violation is found (with a file:line report on stderr), else 0.
Only paths under ``src/`` are scanned; ``examples/`` and ``test/`` are exempt per docs/SRCBANNED.md.
"""

import pathlib
import re
import sys

from tools.ci_tooling.lib import baseline as bl
from tools.ci_tooling.lib import doc_region as dr
from tools.ci_tooling.lib import src_symbols

# Anchored to the repo, not to cwd: a relative Path("src") resolves to nothing from any other
# directory, and a scan of nothing exits 0.
_ROOT = dr.repo_root(__file__)
SRC = pathlib.Path(_ROOT) / "src"
EXTS = {".c", ".cc", ".cpp", ".h", ".hpp", ".ino"}

# Ban #22: runtime dispatch that is not resolvable from the binary. A virtual call reads a vtable
# pointer out of the object and jumps through it, so the target is not known until the object
# exists: the worst-case path cannot be established from the image, the call cannot be inlined or
# devirtualized, and a corrupted object turns every later call into an arbitrary jump. That is the
# same nondeterminism this library refuses everywhere else - fixed pools, compile-time sizing, a
# bounded worst case. RTTI is unbounded on top of it, and std::function type-erases through an
# allocation. Dispatch here is a function pointer in an owned context (ProtoHandler) or a spec/table
# walk (pc_field), both of which the linker can see whole.
_VIRTUAL_MSG = (
    "virtual dispatch; the call target is not known from the binary, so the worst case is not "
    "either. Use a function pointer in an owned context, or a spec/table walk"
)
_INHERIT_MSG = "class hierarchy; compose an owned context instead of inheriting (see ban #22)"
_RTTI_MSG = "RTTI (dynamic_cast/typeid); the type is known at the call site - state it (see ban #22)"
_STDFUNCTION_MSG = "std::function type-erases through an allocation; use a plain function pointer (see ban #22)"

# Ban #23: the conditional expression. MISRA governs src/ and bounds how many conditionals a line may
# carry; a ternary hides a branch inside an expression, so one statement can hold any number of them
# and the decision stops being visible where the value is set. A chain is an if/else-if ladder written
# where the arms cannot be lined up against their conditions, and a nested one makes precedence
# load-bearing for control flow.
#
# The detector is the bare `?`, not a `? ... :` shape, and that is exact rather than approximate:
# comments and string/char literals are already blanked, and once they are gone `?` has exactly one
# meaning left in C. Matching the pair would also miss the multi-line form, where the `?` and its `:`
# land on different lines and the scan is per line.
_TERNARY_MSG = (
    "conditional expression (?:); every assignment and every conditional is its own statement. "
    "Use if / else if / else - declare the variable with the fall-through value, then assign "
    "inside the branch that owns it"
)

# (compiled pattern, ban number, message). Patterns run on comment/string-stripped code. Every
# use-instead pattern (pcdelay, pc_millis, strnlen, gmtime_r) survives because the banned token is
# not on a word boundary there ("pc_millis" has no boundary before "millis", etc.).
BANS = [
    (re.compile(r"\bstrlen\s*\("), 1, "strlen (unbounded read); use strnlen(p, cap)"),
    (re.compile(r"#\s*include\s*<c?stdlib\.h>"), 2, "<stdlib.h>/<cstdlib>; no heap / hidden parse"),
    (re.compile(r"\b(?:malloc|calloc|realloc|free|aligned_alloc)\s*\("), 2, "heap allocation; use fixed BSS buffers"),
    (
        re.compile(r"\b(?:atoi|atol|atoll|strtol|strtoll|strtoul|strtoull|strtod|strtof|qsort|srand|rand)\s*\("),
        2,
        "stdlib parse/util function; hand-roll it",
    ),
    (
        re.compile(r"\b(?:memcpy|memmove|memcmp|memchr|memset)\s*\("),
        24,
        "libc byte op; use mem.cpy / .move / .cmp / .chr / .set / .zero (mmgr/protomem.h), "
        "or proto_raw_read / proto_raw_u16|u32|u64 (mmgr/rawmemcpy.h) for a raw unaligned move",
    ),
    (re.compile(r"#\s*include\s*<c?stdio\.h>"), 25, "<stdio.h>/<cstdio>; nothing in src/ formats"),
    (re.compile(r"#\s*include\s*<c?string\.h>"), 25, "<string.h>/<cstring>; use mem.* / str.*"),
    (re.compile(r"\bauto\b"), 3, "auto keyword; spell the explicit type"),
    (re.compile(r"\bdelay\s*\("), 4, "delay(); use pcdelay(ms) from server/clock/clock.h"),
    (re.compile(r"\b(?:gmtime|localtime|ctime|asctime)\s*\("), 8, "non-reentrant time; use the _r form"),
    (re.compile("—"), 7, "em-dash; use a comma / parentheses / a linking word"),
    (re.compile(r"\bvirtual\b"), 22, _VIRTUAL_MSG),
    (re.compile(r"\b(?:class|struct)\s+\w+\s*:\s*(?:public|protected|private|virtual)\b"), 22, _INHERIT_MSG),
    (re.compile(r"\b(?:dynamic_cast|typeid)\b"), 22, _RTTI_MSG),
    (re.compile(r"\bstd::function\b"), 22, _STDFUNCTION_MSG),
    (re.compile(r"\?"), 23, _TERNARY_MSG),
    # Ban 18 is applied in scan_file, not here: it only fires at file/namespace scope.
    # A `static constexpr` MEMBER of a namespacing struct (LoraReg::REG_FIFO, the
    # pc_radio_ps pattern SYMBOLS.md endorses) is a scoped data table, not a value other
    # code sizes itself against - 616 of 815 sites are those, and flattening them would
    # put 616 more names in the global macro space to fix a problem they do not have.
]

# Blank out // line comments, /* */ block comments, and string / char literals, keeping newlines so
# reported line numbers stay accurate. The leftmost-match rule protects "http://" inside a string.

_INCLUDE = re.compile(r"^\s*#\s*include\b")
_PREPROC = re.compile(r"^\s*#")
# Ban #17: mid-file #include. All includes must be hoisted to the top of a src/ file - an #include that
# appears after real code makes the dependency graph read-order-dependent and hides layering (see the
# ecdsa.cpp / chacha20.cpp hoist). "Real code" is any non-blank line that is not a preprocessor
# directive; comments/strings are already blanked. Preprocessor lines (#if/#define/#pragma/...) and
# blank lines do NOT open the code region, so a top-of-file `#if ... #include ... #endif` block is fine.
# A load-bearing ordered include that genuinely cannot be hoisted (it must run after earlier macros
# resolve) is exempt only with a justified `// PC_ALLOW_LATE_INCLUDE: <reason>` on the include line.
# The two linkage markers do not open the code region either: in C they expand to nothing at all, and
# in C++ to `extern "C" {` and `}`, so an include after one is neither read-order-dependent nor
# layering-hiding. protocore.h opens the block above its include list to give every header underneath
# C linkage in one place.
_MIDINC_MSG = "#include after code; hoist all includes to the top of the file"
_ALLOW_LATE = "PC_ALLOW_LATE_INCLUDE"
_LINKAGE = re.compile(r"^\s*PROTO_(?:BEGIN|END)_DECLS\s*$")

# Ban #18: constexpr. A value the preprocessor cannot see cannot appear in an #if, set another
# knob's #define default, or fail at config time - which is exactly how PC_ENABLE_EDGE_MESH
# became uncompilable. Exempt only where the standard being implemented dictates constexpr,
# with a justified `// PC_ALLOW_CONSTEXPR: <reason>` on the line.
_ALLOW_CONSTEXPR = "PC_ALLOW_CONSTEXPR"
# Only a SCALAR free constant is a value other code sizes itself against. Two shapes at
# file scope are not, and flagging them would be asking for a #define that cannot exist:
#   * a struct/class-typed constant - `constexpr pc_crc_params PC_CRC8_SMBUS = {...}` is a
#     data table, not a size, and an aggregate initializer is not a macro.
#   * an out-of-line definition of a static member - `constexpr FocasCmd FocasCommand::x;`
#     is required before C++17 and is a definition of something already declared in a class.
_CONSTEXPR = re.compile(
    r"\bconstexpr\b\s+(?:u?int(?:8|16|32|64)_t|size_t|int|long|short|char|unsigned(?:\s+\w+)?|bool|float|double)"
    r"\s+\w+\s*[=;]"
)
# Must NOT require the brace on the same line: this codebase uses Allman braces, so
# `struct LoraReg` and its `{` are on separate lines. A trailing `;` means a forward
# declaration, which opens no body.
_AGGREGATE = re.compile(r"\b(?:struct|class|union)\b[^;{]*$|\b(?:struct|class|union)\b[^;{]*\{")
_CONSTEXPR_MSG = (
    "constexpr at file/namespace scope; #define it in protocore_config.h so the "
    "preprocessor can see it (justify a standard-dictated one with "
    "// PC_ALLOW_CONSTEXPR: <reason>)"
)


# Ban #19: a function-local array. Stack memory is the one allocation this library does not
# account for: every other byte is a fixed BSS pool sized at config time, so peak RAM is a
# number you can compute before flashing - but a local array is invisible to that accounting,
# and worst-case stack depth becomes whatever the deepest call chain happens to allocate.
# The work buffers already exist and are reentrant by construction, so there is no cost to
# using them: pc_plaintext_alloc()/PlaintextScope borrows from the caller's OWN per-worker arena
# (one slot per worker plus the ghost, PC_REG_POOL_SLOTS), and crypto leaf math takes fixed offsets in
# crypto_work via the region map. Exempt: `static` locals (already BSS - ban 16 and
# check_owned_context.py own those), and a justified `// PC_ALLOW_STACK_ARRAY: <reason>`.
_ALLOW_STACK_ARRAY = "PC_ALLOW_STACK_ARRAY"

# Ban #20: snprintf and vsnprintf. A format string is parsed at
# RUNTIME, every call, to rediscover what the code already knew at compile time - roughly 3x
# the cost of appending the pieces directly, plus it drags in the libc float formatter. Build
# the frame instead: pc_sb (shared_primitives/strbuf.h) bump-appends into a caller-owned
# buffer and latches ok=false the first time something would not fit, so overflow is one flag
# test at the end rather than a truncation nobody notices. It also carries its own capacity,
# which is why this ban is swept BEFORE #19: it makes each buffer's size explicit at the
# pc_sb init, so turning the array into a borrowed pointer can no longer silently change a
# sizeof() into 4.
_SNPRINTF = re.compile(r"\bv?snprintf\s*\(")
_ALLOW_SNPRINTF = "PC_ALLOW_SNPRINTF"
_SNPRINTF_MSG = (
    "snprintf/format-string formatting; build the frame with pc_sb (shared_primitives/"
    "strbuf.h) - pc_sb_put/pc_sb_u32/pc_sb_json then pc_sb_finish (justify a true "
    "exception with // PC_ALLOW_SNPRINTF: <reason>)"
)
# Anchored at the start of a statement and requiring `;`, `=` or `{` after the subscripts, so
# a USE (`buf[i] = x;`, `return t[n];`) can never match - a declaration is the only shape with
# a type token in front of the name. Multi-dimensional subscripts are one match, not two.
# The separator between type and name must be REAL (whitespace, or a pointer star): with an
# optional one the engine backtracks and splits a single identifier, matching `out[i] = x;` as
# type `o` + name `ut`. That one missing `\s+` reported 1990 assignments as declarations.
_STACK_ARRAY = re.compile(
    r"^\s*(?:const\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+|union\s+)*"
    r"(?!return\b|else\b|case\b|delete\b|new\b)"
    r"[A-Za-z_]\w*(?:\s*::\s*\w+)*(?:\s*<[^;>]*>)?(?:\s+|\s*\*+\s*)"
    r"(\w+)\s*(?:\[[^\]]*\])+\s*[;={]"
)
_STACK_ARRAY_MSG = (
    "function-local array; stack is outside the deterministic footprint. Borrow it: "
    "PlaintextScope + pc_plaintext_alloc() for handler/IO buffers (fail closed on null), or a "
    "crypto_work region for crypto leaf math (justify a true exception with "
    "// PC_ALLOW_STACK_ARRAY: <reason>)"
)


def scan_file(path):
    """Return a list of (path, line_no, ban_no, message) violations in one file."""
    try:
        code = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    clean = src_symbols.blank_comments_and_strings(code)
    raw_lines = code.splitlines()
    hits = []
    seen_code = False
    pp_cont = False  # previous line was a preprocessor directive continued with a trailing backslash
    brace = 0  # nesting depth, for ban 18's file/namespace-scope test
    agg_at = []  # depths at which a struct/class/union body opened
    agg_pending = False  # saw a struct/class/union head, waiting for its brace (Allman style)
    ns_depth = 0  # open namespace blocks; brace beyond this means a function body
    ns_pending = False  # saw a namespace head, waiting for its brace
    for line_no, line in enumerate(clean.splitlines(), 1):
        # Ban 18: only a FREE constexpr (file or namespace scope) is a value other code
        # sizes itself against. Excluded: a member of a namespacing struct (a scoped data
        # table), and a FUNCTION-LOCAL constant - the latter is visible to one function,
        # so it can never feed preprocessor arithmetic, and turning it into a #define
        # would leak it out of the function into the rest of the translation unit.
        in_block = brace > ns_depth
        if _CONSTEXPR.search(line) and not agg_at and not in_block and _ALLOW_CONSTEXPR not in raw_lines[line_no - 1]:
            hits.append((str(path), line_no, 18, _CONSTEXPR_MSG))
        # Ban 19: the mirror image - only a FUNCTION-LOCAL array is stack. A member of a
        # struct/class/union is the owned-context storage this codebase already mandates
        # (opcua's resp[2048] and udp's cap_buf[2048] are fields, not stack), and a
        # file-scope array is BSS. `static` locals are BSS too and belong to ban 16.
        if (
            in_block
            and not agg_at
            and _STACK_ARRAY.match(line)
            and not re.search(r"\bstatic\b", line)
            and _ALLOW_STACK_ARRAY not in raw_lines[line_no - 1]
        ):
            hits.append((str(path), line_no, 19, _STACK_ARRAY_MSG))
        if _SNPRINTF.search(line) and _ALLOW_SNPRINTF not in raw_lines[line_no - 1]:
            hits.append((str(path), line_no, 20, _SNPRINTF_MSG))
        # Allman braces: `struct LoraReg` and its `{` are on separate lines, so the
        # aggregate is remembered as pending and claims the next brace that opens.
        if re.match(r"\s*namespace\b", line):
            ns_pending = True
        if _AGGREGATE.search(line):
            agg_pending = True
        if "{" in line and agg_pending:
            agg_at.append(brace + 1)
            agg_pending = False
        elif "{" in line and ns_pending:
            ns_depth += 1
            ns_pending = False
        brace += line.count("{") - line.count("}")
        agg_at = [d for d in agg_at if d <= brace]
        ns_depth = min(ns_depth, brace)
        for pattern, ban_no, message in BANS:
            if not pattern.search(line):
                continue
            # Ban 18 is the one ban carrying a per-line justification, for a constexpr
            # the standard being implemented actually dictates. Read the RAW line: the
            # marker lives in a comment, and comments are blanked in `clean`.
            if ban_no == 18 and _ALLOW_CONSTEXPR in raw_lines[line_no - 1]:
                continue
            hits.append((str(path), line_no, ban_no, message))
        stripped = line.strip()
        is_pp = pp_cont or bool(_PREPROC.match(line))  # a `\`-continued #if spans lines - all are preprocessor
        if _INCLUDE.match(line) and not pp_cont:
            if seen_code and _ALLOW_LATE not in raw_lines[line_no - 1]:
                hits.append((str(path), line_no, 17, _MIDINC_MSG))
        elif stripped and not is_pp and not _LINKAGE.match(line):
            seen_code = True
        pp_cont = is_pp and stripped.endswith("\\")
    return hits


def _norm(path):
    """Repo-relative, forward slashes: the shape the committed baseline is keyed on.

    Scanning is anchored to the repo so cwd cannot make it a no-op, which makes the scanned
    paths absolute; the key has to come back to repo-relative or every recorded site reads as
    new, and it must not carry the host's separator either.
    """
    s = str(path).replace("\\", "/")
    root = _ROOT.replace("\\", "/").rstrip("/") + "/"
    if s.startswith(root):
        return s[len(root) :]
    return s


def collect(argv):
    if "--all" in argv:
        return [p for p in SRC.rglob("*") if p.suffix in EXTS]
    return [f for f in argv if pathlib.Path(f).suffix in EXTS and _norm(f).startswith("src/")]


# Bans 18 and 20 carry no baseline any more: their sweeps reached zero, so they fail on sight. Bans
# 19, 23, 24 and 25 ride one while theirs run down. Drop a number from this set the moment its sweep
# reaches zero, and the ban fails on sight from then on.
BASELINED = {19, 23, 24, 25}
BASELINE = bl.path_for(__file__, "sweep_baseline")


_DECL = re.compile(r"\bconstexpr\b[\w:<>*&\s]*?(\w+)\s*[=({\[]")


def _key(v):
    """Identify a violation by file + DECLARED NAME, not by line and not by message.

    Not the line number: a site must not come back from the dead because lines above it
    moved. Not the message either - it is identical for every constexpr, so keying on it
    collapsed 816 sites onto 93 file-level keys, and a new constexpr added to a file that
    already had one was waved straight through. The declared name is stable across edits
    above it and unique within a file.
    """
    raw_path, line_no, ban_no, message = v
    # The baseline is a committed file read on every platform, so the key must not carry the host's
    # path separator. rglob() yields WindowsPath on Windows, which made every recorded site read as
    # new there: the gate reported the entire ratcheted set as a regression and was unusable
    # locally, while CI on Linux passed.
    path = _norm(raw_path)
    if ban_no in (18, 19):
        # falls through to the name-based key; ban 19 wraps this with an occurrence ordinal
        try:
            raw = pathlib.Path(raw_path).read_text(encoding="utf-8", errors="replace").splitlines()
            src_line = raw[line_no - 1]
            m = _DECL.search(src_line) if ban_no == 18 else _STACK_ARRAY.match(src_line)
            if m:
                return f"{path}|{ban_no}|{m.group(1)}"
        except (OSError, IndexError):
            pass
        # no parseable name (a continuation line): fall back to the line's own text
        return f"{path}|{ban_no}|line{line_no}"
    if ban_no == 20:
        # every snprintf shares one message, so the ordinal wrapper does the disambiguating
        return f"{path}|20|snprintf"
    if ban_no == 23:
        # one message for every ternary, and a line can hold several; the ordinal wrapper separates
        # them, so removing one leaves the survivors' ordinals inside the recorded set
        return f"{path}|23|ternary"
    return f"{path}|{ban_no}|{message}"


def _ordinal_keys(violations):
    """Map each ban-19 violation to a key that is unique PER SITE.

    A bare `path|19|name` key repeats the mistake ban 18's baseline made: ecdsa.cpp declares
    `t[8]` in three different functions, so one key would cover all three and a re-added
    fourth would pass unseen. Numbering each occurrence keeps the recorded set prefix-closed
    under removal - deleting a site leaves the survivors' ordinals inside the baseline, while
    ADDING one mints an ordinal nobody recorded. So the count can fall and never rise, which
    is the whole contract. The violation tuple carries its line number, so it is unique and
    safe to index by.
    """
    seen, out = {}, {}
    for v in violations:
        base = _key(v)
        seen[base] = seen.get(base, 0) + 1
        out[v] = f"{base}#{seen[base]}"
    return out


def main(argv):
    violations = []
    for path in collect(argv):
        violations.extend(scan_file(path))

    # Bans 19 and 20 ride a baseline while task 26 works them down: 987 stack arrays and 236
    # snprintf calls predate both rules. Ban 18 lost its baseline once all 165 sites were
    # converted. Every other ban is absolute and fails on sight.
    ratcheted = [v for v in violations if v[2] in BASELINED]
    others = [v for v in violations if v[2] not in BASELINED]
    ordinals = _ordinal_keys(ratcheted)

    if "--baseline" in argv:
        n = bl.save(BASELINE, (ordinals[v] for v in ratcheted))
        print(f"check_src_banned: recorded {n} known site(s) as the floor " f"(bans {sorted(BASELINED)})")
        return 0

    new_ratcheted, known, fixed = bl.filter_new(ratcheted, lambda v: ordinals[v], BASELINE)
    violations = others + new_ratcheted

    if violations:
        print("check_src_banned: banned constructs in src/ (see docs/SRCBANNED.md):", file=sys.stderr)
        for path, line_no, ban_no, message in violations:
            print(f"  {path}:{line_no}: [ban #{ban_no}] {message}", file=sys.stderr)
        print(
            f"check_src_banned: {len(violations)} violation(s) - fix them; src/ is fully constrained.", file=sys.stderr
        )
        return 1
    if known:
        print(
            f"check_src_banned: OK - no new violations "
            f"({known} known ratcheted site(s) remain{f', {fixed} fixed' if fixed else ''})"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
