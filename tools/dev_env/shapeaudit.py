"""What a module's design IS, read off the file, and where it diverges from the golden's.

The golden is not described here. It is PARSED: sha256.h and sha256.c go through the same reader
every target does, and what comes out is the reference. Editing the golden moves every target with
it, and no axis list in this file can go stale against the module it claims to describe.

  shapeaudit.py design <module.h>        the design this file HAS, as JSON
  shapeaudit.py diff   <module.h> ...    where it diverges from the golden's
  shapeaudit.py families [root ...]      every module grouped by the divergence it shares

WHY A SEQUENCE AND NOT A CHECKLIST. A checklist says an element is present; it cannot say the
config include sits above the enable gate, which is the whole point of the gate. So a design is the
ORDERED run of top-level constructs, each labelled with a kind the reader can name without knowing
which module it is reading, plus the traits that survive being renamed.

WHY THE NAMES ARE SUBSTITUTED. sha256's design says `extern Sha256Ns Sha256;` and dnp3's says
`extern Dnp3Ns Dnp3;`. Those are the same design. The reader learns each file's identity - module,
object, gate, guard, borrow - and rewrites the golden's identity into the target's before comparing,
so what is left is real divergence rather than the module's own name.
"""

import io, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from codemask import code_mask

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GOLDEN = "src/crypto/hash/sha256/sha256.h"


# --- identity ---------------------------------------------------------------


def identity(htext, ctext, hpath):
    """The names this module calls itself by, read off the pair.

    Everything else compares after these are substituted out, so a wrong one here shows up as a
    whole file of false divergence rather than as a subtle one.
    """
    mod = os.path.basename(hpath)[:-2]
    ident = {"module": mod}
    m = re.search(r"^\s*#\s*ifndef\s+(\w+)", htext, re.M)
    ident["guard"] = m.group(1) if m else ""
    m = re.search(r"^\s*#\s*if\s+(PROTOCORE_ENABLE_\w+)", htext, re.M)
    ident["gate"] = m.group(1) if m else ""
    m = re.search(r"^\s*extern\s+(?:const\s+)?(\w+)\s+(\w+)\s*;", htext, re.M)
    ident["ns"], ident["object"] = (m.group(1), m.group(2)) if m else ("", "")
    m = re.search(r"\b(PROTOCORE_\w+_BORROW)\b", htext + ctext)
    ident["borrow"] = m.group(1) if m else ""
    return ident


def substitute(text, src, dst):
    """The golden's text with its identity rewritten into the target's.

    Longest first: PROTOCORE_SHA256_BORROW has to be replaced before SHA256, or the tail of it is
    rewritten in place and the token stops being a token.
    """
    pairs = [(src[k], dst[k]) for k in ("borrow", "gate", "guard", "ns", "object", "module") if src.get(k)]
    pairs += [(src["module"].upper(), dst["module"].upper())]
    for a, b in sorted(pairs, key=lambda p: -len(p[0])):
        if a and b:
            text = re.sub(r"\b%s\b" % re.escape(a), b, text)
    return text


# --- the reader -------------------------------------------------------------

# A top-level construct's kind. Every name here is one the reader can decide from the text alone -
# none of them says "sha256", and none says which module is expected to have it.
DIRECTIVE = re.compile(r"^[ \t]*#[ \t]*(\w+)(.*)$", re.M)


def _blank_noncode(s):
    """s with comment and literal bytes turned to spaces, newlines kept so offsets and lines hold."""
    m = code_mask(s)
    return "".join(c if (m[i] or c == "\n") else " " for i, c in enumerate(s))


def _comment_runs(s):
    """(start, end, text) for every comment in s, so a doc block is an element and not a gap."""
    m, out, i, n = code_mask(s), [], 0, len(s)
    while i < n:
        if m[i] or s[i] == "\n":
            i += 1
            continue
        if s[i] in "\"'":  # a literal, not a comment
            i += 1
            continue
        j = i
        while j < n and (not m[j] or s[j] in " \t\r\n"):
            j += 1
        k = j  # trailing whitespace is not part of the comment
        while k > i and s[k - 1] in " \t\r\n":
            k -= 1
        out.append((i, k, s[i:k]))
        i = j
    return out


TYPEDEF = re.compile(r"\btypedef\s+(struct|enum|union)\b", re.M)
FUNC_DEF = re.compile(r"(?P<head>^[A-Za-z_][^;{}#]*?)\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}]*)\)\s*\{", re.M)
FUNC_DECL = re.compile(r"(?P<head>^[A-Za-z_][^;{}#]*?)\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}]*)\)\s*;", re.M)
EXTERN = re.compile(r"^\s*extern\s+(?P<type>(?:const\s+)?[\w\s\*]+?)\s+(?P<name>\w+)\s*(?:\[[^\]]*\])?\s*;", re.M)
OBJDEF = re.compile(r"^(?P<qual>(?:static\s+|const\s+)*)(?P<type>\w+)\s+(?P<name>\w+)\s*(?P<arr>\[[^\]]*\])?\s*=", re.M)
STATIC_ASSERT = re.compile(r"\bstatic_assert\s*\(", re.M)
# The borrow tested against null, in any position. `pre` captures what precedes the identifier so a
# `->work` / `.work` struct member can be told apart from the borrow itself.
NULL_BORROW_TEST = re.compile(
    r"(?P<pre>[-.>\w]{0,2})\bwork\s*(?:==|!=)\s*(?:NULL|0|nullptr)\b|!\s*(?P<pre2>[-.>]{0,2})\bwork\b\s*(?=[)&|])"
)

KEYWORD = ("typedef", "return", "if", "while", "for", "switch", "else", "do", "sizeof", "case")


def _close_brace(s, at):
    """Index just past the '}' matching the '{' at `at`, on blanked text."""
    depth, i = 0, at
    while i < len(s):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(s)


def close_paren(s, at):
    """Index just past the ')' matching the '(' at `at`, on blanked text."""
    depth, i = 0, at
    while i < len(s):
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(s)


def read_design(path):
    """The ordered design of one file: a list of {kind, ...} elements, in the order they appear."""
    raw = io.open(path, encoding="utf-8").read()
    code = _blank_noncode(raw)
    els = []

    def at(pos):
        return raw.count("\n", 0, pos) + 1

    def take(start, kind, **kw):
        d = {"kind": kind, "line": at(start)}
        d.update(kw)
        els.append((start, d))

    # Comments first: the license run and the @file block are constructs in their own right, and
    # every other comment is prose a construct carries rather than a construct.
    for a, b, txt in _comment_runs(raw):
        low = txt.lower()
        if "spdx-license-identifier" in low:
            take(a, "license")
        elif "@file" in txt:
            take(a, "filedoc", tags=sorted(set(re.findall(r"@(\w+)", txt))))
        elif re.search(r"\b\w+_BORROW\b", txt):
            take(a, "borrow_note")

    for m in DIRECTIVE.finditer(code):
        word, rest = m.group(1), m.group(2).strip()
        s = m.start()
        if word == "include":
            take(s, "include", path=rest.strip("\"<>"), system=rest.startswith("<"))
        elif word == "ifndef" and re.match(r"^\w+_H$", rest):
            take(s, "guard_open", name=rest)
        elif word == "define":
            nm = re.match(r"(\w+)", rest)
            nm = nm.group(1) if nm else ""
            tail = rest[len(nm) :]
            fn = bool(re.match(r"\(", tail))
            body = (tail[close_paren(tail, 0) :] if fn else tail).strip()
            take(s, "define", name=nm, fn=fn, empty=body in ("", "\\"), body=body)
        elif word in ("if", "ifdef"):
            take(s, "cond_open", expr=rest)
        elif word == "elif":
            take(s, "cond_elif", expr=rest)
        elif word == "else":
            take(s, "cond_else")
        elif word == "endif":
            take(s, "cond_close")
        elif word in ("error", "warning", "pragma"):
            take(s, word, expr=rest)

    # BEGIN/END_DECLS and the attribute macros are bare identifiers, not directives.
    for m in re.finditer(r"^[ \t]*(PROTOCORE_BEGIN_DECLS|PROTOCORE_END_DECLS)[ \t]*$", code, re.M):
        take(m.start(), "decls_open" if "BEGIN" in m.group(1) else "decls_close")
    for m in re.finditer(r"^[ \t]*(PROTOCORE_\w+_HOT|PROTOCORE_\w+_ATTR)[ \t]*$", code, re.M):
        take(m.start(), "attribute", name=m.group(1))

    # A record's members are read as its members, never as top-level constructs: a function-pointer
    # member has a prototype's shape and an initialised member has an object's.
    spans = []
    for m in TYPEDEF.finditer(code):
        brace = code.find("{", m.start())
        semi = code.find(";", m.start())
        if brace == -1 or (semi != -1 and semi < brace):  # typedef of an existing type
            take(m.start(), "typedef_alias")
            continue
        end = _close_brace(code, brace)
        close = code.find(";", end)
        spans.append((m.start(), close + 1))
        nm = re.search(r"(\w+)\s*;", code[end : close + 1])
        take(
            m.start(),
            "record",
            form=m.group(1),
            name=nm.group(1) if nm else "",
            role=_role(nm.group(1) if nm else ""),
            members=_members(code[brace + 1 : end - 1], m.group(1)),
        )

    # static_assert is a declaration ending in ';', so it matches a prototype's shape exactly. Its
    # span is claimed here and every later reader steps over it: without that, sha256's own
    # `static_assert(SHA256_OFF_STATE + sizeof(uint32_t) * 8 <= ..., "...")` reads as a function
    # called `sizeof` and the golden diverges from itself.
    for m in STATIC_ASSERT.finditer(code):
        end = code.find(";", close_paren(code, code.index("(", m.end() - 1)))
        end = len(code) if end == -1 else end + 1
        spans.append((m.start(), end))
        take(
            m.start(),
            "static_assert",
            cites_borrow=bool(re.search(r"\w+_BORROW", code[m.start() : end])),
            text=" ".join(code[m.start() : end].split()),
        )

    # Functions: definitions first, then the declarations that are not inside one.
    for m in FUNC_DEF.finditer(code):
        if any(a <= m.start() < b for a, b in spans):
            continue
        head = m.group("head")
        if re.search(r"\b(%s)\b" % "|".join(KEYWORD), head):
            continue
        brace = code.index("{", m.end() - 1)
        end = _close_brace(code, brace)
        spans.append((m.start(), end))
        take(
            m.start(),
            "function",
            name=m.group("name"),
            static="static" in head,
            inline="inline" in head,
            params=_params(m.group("params")),
            ret=_ret(head),
            locals=_local_arrays(code[brace:end], raw[brace:end]),
        )
    for m in FUNC_DECL.finditer(code):
        if any(a <= m.start() < b for a, b in spans):
            continue
        if re.search(r"\b(%s)\b" % "|".join(KEYWORD), m.group("head")):
            continue
        take(m.start(), "prototype", name=m.group("name"), params=_params(m.group("params")), ret=_ret(m.group("head")))

    # A test of the BORROW against null, wherever it appears. Not restricted to an `if` condition:
    # euromap77 spelled it `Euromap77.ok = work != NULL;`, making the entry's whole success
    # criterion a condition that is always true, and an if-shaped search walked past it.
    #
    # A `work` reached through `->` or `.` is a struct member that happens to carry the name. It is
    # not the borrow, it can legitimately be null, and ssh transport has two of them.
    for m in NULL_BORROW_TEST.finditer(code):
        pre = (m.group("pre") or "") + (m.group("pre2") or "")
        if pre.strip().endswith((">", ".")):
            continue
        take(m.start(), "null_borrow_test", text=" ".join(m.group(0).split()))

    for m in EXTERN.finditer(code):
        take(m.start(), "extern", type=m.group("type").strip(), name=m.group("name"))
    for m in OBJDEF.finditer(code):
        if any(a <= m.start() < b for a, b in spans):
            continue
        if m.group("type") in KEYWORD:
            continue
        take(
            m.start(),
            "object",
            name=m.group("name"),
            type=m.group("type"),
            static="static" in m.group("qual"),
            const="const" in m.group("qual"),
            array=bool(m.group("arr")),
        )

    els.sort(key=lambda p: p[0])
    return [d for _, d in els]


# A declaration inside a function body that reserves storage by extent: `uint8_t buf[64];`. The
# golden reserves nothing this way - every region it works in is carved out of the borrow at a
# compile-time offset - so a local array is storage the arena never sized and the stack pays for.
#
# `const` is NOT excluded. A const local with a literal extent is still an object with automatic
# storage: whether it is promoted to .rodata or built on the stack on every call is the compiler's
# choice, not the source's, so it counts the same as a mutable one. Only its `const` is recorded, so
# the report can say which kind each one is.
LOCAL_ARRAY = re.compile(
    r"^[ \t]+(?!return\b|case\b|else\b)(?P<qual>(?:static\s+|const\s+|volatile\s+|register\s+)*)"
    r"(?P<type>[A-Za-z_]\w*(?:\s*\*)*)\s+(?P<name>[A-Za-z_]\w*)\s*\[(?P<extent>[^\];]*)\]\s*(?:=|;)",
    re.M,
)


def _local_arrays(body, raw):
    """Every function-local DATA array that reserves storage, as {name, type, extent, const, ...}.

    Data only. A `char` array and anything initialised from a string literal are skipped whatever
    their qualifiers: those live in flash, and moving one into the borrow does not save RAM, it
    SPENDS it - the string stops being addressed where it is stored and starts being copied into
    the arena. The check exists to find working buffers built on the stack, and a string is not one.

    @p body is the blanked text (so brackets and braces are syntax) and @p raw is the same span
    unblanked (so an initialiser's opening quote is still visible).
    """
    out = []
    for m in LOCAL_ARRAY.finditer(body):
        typ = m.group("type").strip()
        if typ in KEYWORD or "char" in typ:
            continue
        after = raw[m.end() - 1 :].lstrip()
        if after.startswith("=") and after[1:].lstrip().startswith('"'):
            continue  # initialised from a string literal: flash, not a working buffer
        extent = " ".join(m.group("extent").split())
        out.append(
            {
                "name": m.group("name"),
                "type": typ,
                "extent": extent,
                "const": "const" in m.group("qual"),
                "static": "static" in m.group("qual"),
                # A literal extent is a fixed reservation the borrow could have carried; a named or
                # computed one may be a VLA, which is a different defect with a different fix.
                "literal": bool(re.fullmatch(r"\d+[uUlL]*", extent)),
            }
        )
    return out


def _role(name):
    """What a record's name says it is. A suffix, not an identity: the golden distinguishes Args
    from Ns from Ctx, and a target's own suffix answers the same question about the target."""
    for suf in ("Args", "Ns", "Ctx", "Internal", "Handle"):
        if name.endswith(suf):
            return suf
    return "type"


FNPTR_MEMBER = re.compile(r"^(?P<ret>.*?)\(\s*\*\s*(?P<qual>const\s+|volatile\s+)*(?P<name>[A-Za-z_]\w*)\s*\)\s*\((?P<params>.*)\)$")


def _members(body, form):
    """A record's members, as {name, fnptr, type}.

    A function-pointer member is read from inside its parentheses. Taking the last identifier on the
    line instead - which is what a plain trailing-name match does - names every entry after the last
    parameter of its own signature, so all four of the golden's entries come out called `work` and
    every question asked about them compares one member against itself.
    """
    if form == "enum":
        return [x.strip().split("=")[0].strip() for x in body.split(",") if x.strip()]
    out = []
    for line in body.split(";"):
        line = " ".join(line.split())
        if not line:
            continue
        m = FNPTR_MEMBER.match(line)
        if m:
            out.append(
                {
                    "name": m.group("name"),
                    "fnptr": True,
                    "type": "%s(*)(%s)" % (m.group("ret").strip(), " ".join(m.group("params").split())),
                    "params": _params(m.group("params")),
                }
            )
            continue
        m = re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])?$", line)
        if m:
            out.append({"name": m.group(1), "fnptr": False, "type": line[: m.start(1)].strip()})
    return out


def _params(txt):
    txt = " ".join(txt.split())
    if txt in ("", "void"):
        return []
    out, depth, cur = [], 0, []
    for c in txt:
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def _ret(head):
    head = " ".join(head.split())
    return re.sub(r"^(static|inline|const|PROTOCORE_\w+)\s+", "", head).strip()


# --- traits -----------------------------------------------------------------


def traits(design, which):
    """The design reduced to the questions a comparison can answer, all derived from the elements.

    A trait is a generic question - "which includes sit above the enable gate" - so the golden's
    ANSWER is the reference and nothing here states what that answer should be.
    """
    kinds = [d["kind"] for d in design]
    t = {"file": which}
    t["order"] = _collapse(kinds)
    t["counts"] = {k: kinds.count(k) for k in sorted(set(kinds))}

    inc = [d for d in design if d["kind"] == "include"]
    t["includes"] = [d["path"] for d in inc]
    t["system_includes"] = [d["path"] for d in inc if d["system"]]

    gate = next((i for i, d in enumerate(design) if d["kind"] == "cond_open" and "ENABLE_" in d.get("expr", "")), None)
    t["gate_present"] = gate is not None
    above = design[:gate] if gate is not None else design
    below = design[gate:] if gate is not None else []
    t["includes_above_gate"] = [d["path"] for d in above if d["kind"] == "include"]
    t["includes_below_gate"] = [d["path"] for d in below if d["kind"] == "include"]
    t["constructs_above_gate"] = _collapse(
        [d["kind"] for d in above if d["kind"] not in ("license", "filedoc", "include", "guard_open", "define")]
    )

    recs = [d for d in design if d["kind"] == "record"]
    t["record_roles"] = sorted({d["role"] for d in recs})
    t["ns_records"] = [d["name"] for d in recs if d["role"] == "Ns"]
    t["args_records"] = sorted(d["name"] for d in recs if d["role"] == "Args")
    t["state_records"] = sorted(d["name"] for d in recs if d["role"] in ("Ctx", "Internal", "Handle"))
    t["public_records"] = sorted(d["name"] for d in recs if d["role"] == "type")

    ns = next((d for d in recs if d["role"] == "Ns"), None)
    t["ns_entry_types"] = sorted({m["type"] for m in ns["members"] if m["fnptr"]}) if ns else []
    t["ns_result_members"] = [m["name"] for m in ns["members"] if not m["fnptr"] and not m["name"].endswith("_args")] if ns else []
    t["ns_arg_members"] = [m["name"] for m in ns["members"] if m["name"].endswith("_args")] if ns else []

    t["externs"] = sorted((d["type"], d["name"]) for d in design if d["kind"] == "extern")
    t["extern_types"] = sorted({d["type"] for d in design if d["kind"] == "extern"})

    fns = [d for d in design if d["kind"] == "function"]
    t["nonstatic_functions"] = sorted(d["name"] for d in fns if not d["static"])
    t["public_param_shapes"] = sorted({tuple(d["params"]) for d in fns if not d["static"]})
    t["prototypes"] = sorted(d["name"] for d in design if d["kind"] == "prototype")
    t["prototype_param_shapes"] = sorted({tuple(d["params"]) for d in design if d["kind"] == "prototype"})

    t["static_asserts_citing_borrow"] = sum(1 for d in design if d["kind"] == "static_assert" and d.get("cites_borrow"))
    offs = [d for d in design if d["kind"] == "define" and "_OFF_" in d.get("name", "")]
    t["offset_defines"] = sorted(d["name"] for d in offs)
    t["region_macros"] = sorted(d["name"] for d in design if d["kind"] == "define" and d.get("fn"))
    asserts = [d for d in design if d["kind"] == "static_assert"]
    t.update(_offset_chain(offs, asserts))
    t.update(_region_alignment([d for d in design if d["kind"] == "define" and d.get("fn")], asserts))
    t["file_statics"] = sorted(d["name"] for d in design if d["kind"] == "object" and d.get("static") and not d.get("const"))

    # Any test of the BORROW against null, in any position - not only an `if` condition. euromap77
    # spelled it `Euromap77.ok = work != NULL;`, which made the entry's whole success criterion a
    # condition that is always true, and an `if`-shaped search walked straight past it.
    t["null_borrow_tests"] = sum(1 for d in design if d["kind"] == "null_borrow_test")

    # Storage reserved on the stack instead of carved out of the borrow. Counted, and listed with
    # the function that reserves it, because the fix is per-array: each one becomes a region.
    la = [dict(a, fn=d["name"]) for d in design if d["kind"] == "function" for a in d.get("locals", [])]
    t["local_arrays"] = ["%s.%s[%s]" % (a["fn"], a["name"], a["extent"]) for a in la]
    t["local_arrays_literal"] = [x for x, a in zip(t["local_arrays"], la) if a["literal"]]
    t["local_arrays_const"] = [x for x, a in zip(t["local_arrays"], la) if a["const"]]
    t["ns_definition"] = sorted(d["name"] for d in design if d["kind"] == "object" and d["type"].endswith("Ns"))
    return t


def _offset_chain(offs, asserts):
    """Whether the borrow's regions are laid out as a chain, and whether the assert covers the end.

    The golden's offsets are a running sum: the first is zero, and each one after it is the
    PREVIOUS offset plus the size of what sits there. Two regions that do not chain overlap - both
    read the same bytes and one silently scribbles on the other - and an assert that does not name
    the LAST offset sizes the borrow against a region that is not the far end, so the arena is short
    by exactly the last region and nothing says so until it corrupts a neighbour.

    Read positionally, in file order, so an offset chained onto something other than its immediate
    predecessor is reported rather than accepted for merely naming another offset.
    """
    out = {
        "offset_first_is_zero": None,
        "offset_chain_links": None,
        "offset_chain_intact": None,
        "offset_assert_covers_last": None,
    }
    if not offs:
        return out
    names = [d["name"] for d in offs]
    first = offs[0].get("body", "").strip()
    out["offset_first_is_zero"] = bool(re.fullmatch(r"\(*\s*0[uU]?[lL]*\s*\)*", first))
    links = []
    for prev, cur in zip(offs, offs[1:]):
        body = cur.get("body", "")
        links.append(bool(re.search(r"\b%s\b" % re.escape(prev["name"]), body)))
    out["offset_chain_links"] = links
    out["offset_chain_intact"] = all(links) and out["offset_first_is_zero"]
    # The assert has to bound the LAST region, so it is the last offset's name it must carry.
    cite = [a for a in asserts if a.get("cites_borrow")]
    out["offset_assert_covers_last"] = any(
        re.search(r"\b%s\b" % re.escape(names[-1]), a.get("text", "")) for a in cite
    )
    return out


# A region macro that reaches its bytes through a cast: `((T *)(void *)((w) + X_OFF_Y))`. The cast
# is what introduces an alignment requirement - a macro yielding plain `(w) + OFF` stays uint8_t*,
# which has none.
CAST_REGION = re.compile(r"\(\s*(?P<type>[A-Za-z_]\w*)\s*\*\s*\)\s*\(\s*void\s*\*\s*\)")
# Types whose alignment is 1 by the standard, so an offset assert on them is a tautology. Every
# other type - including a typedef this reader cannot resolve - gets one: a needless assert costs a
# line, and a missing one costs a misaligned pointer with no diagnostic.
ALIGN_ONE = {"char", "uint8_t", "int8_t", "signed", "unsigned", "void", "byte"}
OFF_IN_BODY = re.compile(r"\b(\w*_OFF_\w+)\b")
# `OFF % _Alignof(T) == 0` in any spacing, either spelling of alignof.
ALIGN_ASSERT = re.compile(r"\b(?P<off>\w*_OFF_\w+)\s*%\s*(?:_Alignof|alignof)\s*\(\s*(?P<type>[^)]+?)\s*\)\s*==\s*0")


def _region_alignment(macros, asserts):
    """Which cast regions exist, and which of them an alignment assert actually covers.

    The size assert cannot stand in for this: it bounds the far end of the chain and says nothing
    about where any region begins. One odd-sized region inserted earlier leaves every later cast
    misaligned while the total still fits.

    The arena aligns the base up to PROTOCORE_ARENA_MAX_ALIGN, so the offset is the whole claim -
    and an offset is a compile-time constant, which is why this is a static_assert and never a
    runtime branch.
    """
    regions = []
    for d in macros:
        body = d.get("body", "")
        m = CAST_REGION.search(body)
        if not m:
            continue  # yields uint8_t*: no alignment requirement to state
        if m.group("type") in ALIGN_ONE:
            continue  # alignment 1: the assert would read `OFF % 1 == 0`
        off = OFF_IN_BODY.search(body)
        regions.append({"macro": d["name"], "type": m.group("type"), "offset": off.group(1) if off else ""})
    covered = set()
    for a in asserts:
        for m in ALIGN_ASSERT.finditer(a.get("text", "")):
            covered.add(m.group("off"))
    return {
        "cast_regions": ["%s->%s@%s" % (r["macro"], r["type"], r["offset"]) for r in regions],
        "cast_regions_unaligned": [
            "%s->%s@%s" % (r["macro"], r["type"], r["offset"]) for r in regions if r["offset"] not in covered
        ],
    }


def _collapse(seq):
    """A run of the same kind is one entry: three includes in a row is 'includes', not an order."""
    out = []
    for k in seq:
        if not out or out[-1] != k:
            out.append(k)
    return out


# --- comparison -------------------------------------------------------------

# A trait whose VALUE is the module's own vocabulary is compared by how many, not by which: every
# module's args records are named after itself, and listing them would report every module as
# divergent from every other. The structural traits are compared exactly.
SHAPE_ONLY = (
    "args_records",
    "ns_records",
    "state_records",
    "public_records",
    "externs",
    "extern_types",
    "nonstatic_functions",
    "prototypes",
    "offset_defines",
    "region_macros",
    "file_statics",
    "ns_definition",
    "ns_result_members",
    "ns_arg_members",
    "includes",
    "includes_below_gate",
    "system_includes",
)


def _plain(v):
    """A value through JSON's own vocabulary, so both sides are compared in one.

    The golden's traits are substituted by re-serialising them, which turns every tuple into a
    list. Comparing that against a target's un-round-tripped tuple reports a divergence between two
    values that are the same - the golden diverging from itself, on every module.
    """
    return json.loads(json.dumps(v, default=str))


def compare(gold, tgt):
    """Every trait where the target's answer differs from the golden's, as (trait, gold, target)."""
    out = []
    for k in sorted(set(gold) | set(tgt)):
        if k == "file":
            continue
        g, v = _plain(gold.get(k)), _plain(tgt.get(k))
        if k in SHAPE_ONLY:
            g, v = len(g or []), len(v or [])
        if g == v:
            continue
        out.append((k, _short(g), _short(v)))
    return out


def _short(v, cap=64):
    s = v if isinstance(v, str) else json.dumps(v, default=str)
    return s if len(s) <= cap else s[: cap - 1] + "..."


def design_of(hrel):
    """The (identity, header traits, source traits) of one module."""
    hp = os.path.join(R, hrel.replace("/", os.sep))
    cp = hp[:-2] + ".c"
    htext = io.open(hp, encoding="utf-8").read()
    ctext = io.open(cp, encoding="utf-8").read() if os.path.exists(cp) else ""
    ident = identity(htext, ctext, hp)
    h = traits(read_design(hp), "header")
    c = traits(read_design(cp), "source") if ctext else None
    return ident, h, c


def golden_designs():
    return design_of(GOLDEN)


def diverge(hrel, gid=None, gh=None, gc=None):
    """Every divergence of one module from the golden, header and source, after substitution."""
    if gh is None:
        gid, gh, gc = golden_designs()
    ident, h, c = design_of(hrel)
    subs = lambda x: json.loads(substitute(json.dumps(x), gid, ident))
    out = [("header", k, g, v) for k, g, v in compare(subs(gh), h)]
    if c is None:
        out.append(("source", "exists", "yes", "no"))
    else:
        out += [("source", k, g, v) for k, g, v in compare(subs(gc), c)]
    return ident, out


def modules(roots):
    """Every .h under the roots, repo-relative and sorted."""
    out = []
    for root in roots:
        for dirpath, _, names in os.walk(os.path.join(R, root.replace("/", os.sep))):
            for n in names:
                if n.endswith(".h"):
                    out.append(os.path.relpath(os.path.join(dirpath, n), R).replace("\\", "/"))
    return sorted(out)


def read_tree(roots):
    """Every module under the roots, read once: (rel, divergences, error, identity, header, source)."""
    gid, gh, gc = golden_designs()
    seen, values = [], {}
    for rel in modules(roots):
        if rel == GOLDEN:
            continue
        try:
            ident, h, c = design_of(rel)
            subs = lambda x: json.loads(substitute(json.dumps(x), gid, ident))
            ds = [("header", k, g, v) for k, g, v in compare(subs(gh), h)]
            ds += (
                [("source", "exists", "yes", "no")]
                if c is None
                else [("source", k, g, v) for k, g, v in compare(subs(gc), c)]
            )
        except Exception as ex:
            seen.append((rel, None, "%s: %s" % (type(ex).__name__, ex), None, None, None))
            continue
        seen.append((rel, ds, None, ident, h, c))
        for w, k, _, v in ds:
            values.setdefault("%s.%s" % (w, k), set()).add(v)
    return seen, values


# THE CHECKS. One question each, answered yes or no for any module, and the GOLDEN's own answer is
# what "yes" means: every predicate below takes the golden's traits (gh, gc) beside the target's, so
# none of them states a value. A check that the golden itself fails is a bug in the check, which
# `shapeaudit.py selfcheck` is there to catch.
#
# The verb column is the tool the surface already documents for that shape (`harness.py help
# convert`). A check says which question a module answers no to; the verb says which tool answers it.
CHECKS = [
    (
        "namespace",
        "is there one <X>Ns, externed, and defined in the .c?",
        lambda h, c, gh, gc: len(h["ns_records"]) == len(gh["ns_records"])
        and len(h["externs"]) == len(gh["externs"])
        and len(c["ns_definition"]) == len(gc["ns_definition"]),
        "convert gen",
    ),
    (
        "no flat decls",
        "is the header free of free-function declarations?",
        lambda h, c, gh, gc: len(h["prototypes"]) == len(gh["prototypes"]),
        "convert gen",
    ),
    (
        "operands are members",
        "is every operand an args member rather than a parameter?",
        lambda h, c, gh, gc: len(h["args_records"]) > 0 or len(gh["args_records"]) == 0,
        "convert gen",
    ),
    (
        "entries take borrow",
        "does every entry take exactly the borrow and nothing else?",
        lambda h, c, gh, gc: h["ns_entry_types"] == gh["ns_entry_types"],
        "convert gen",
    ),
    (
        "ctx private",
        "is the state record kept out of the header?",
        lambda h, c, gh, gc: len(h["state_records"]) == len(gh["state_records"]),
        "convert pimpl",
    ),
    (
        "no internal handle",
        "is state reached through the borrow rather than a struct <X>Internal *?",
        lambda h, c, gh, gc: not any(n.endswith(("Internal", "Handle")) for n in h["state_records"] + c["state_records"]),
        "convert pimpl",
    ),
    (
        "no file statics",
        "is the .c free of file-scope mutable state?",
        lambda h, c, gh, gc: len(c["file_statics"]) == len(gc["file_statics"]),
        "convert funnel",
    ),
    (
        "borrow carved",
        "does the .c carve the borrow at compile-time offsets under one static_assert?",
        lambda h, c, gh, gc: bool(c["offset_defines"]) == bool(gc["offset_defines"])
        and c["static_asserts_citing_borrow"] == gc["static_asserts_citing_borrow"]
        and bool(c["region_macros"]) == bool(gc["region_macros"]),
        "convert funnel",
    ),
    (
        "no local arrays",
        "is every working buffer a borrow region rather than a local array?",
        # A local array is storage the arena never sized, and the fix is the same for all of them:
        # it becomes a region at an offset in the borrow. const counts - whether a const local with
        # a literal extent lands in .rodata or is built on the stack per call is the compiler's
        # choice, not the source's.
        lambda h, c, gh, gc: len(c["local_arrays"]) == len(gc["local_arrays"]),
        "convert funnel",
    ),
    (
        "offsets chain",
        "does each region offset build on the one before it, starting at zero?",
        lambda h, c, gh, gc: c["offset_chain_intact"] == gc["offset_chain_intact"],
        "convert funnel",
    ),
    (
        "assert covers last",
        "does the static_assert size the borrow against the LAST region?",
        lambda h, c, gh, gc: c["offset_assert_covers_last"] == gc["offset_assert_covers_last"],
        "convert funnel",
    ),
    (
        "offsets aligned",
        "does every region reached through a cast assert its offset is aligned for that type?",
        # The size assert bounds the far end and says nothing about where a region begins, so this
        # is a separate question. Zero unasserted cast regions is the golden's answer, and the
        # golden earns it rather than being exempted: sha256.c asserts SHA256_OFF_CTX against
        # alignof(Sha256Ctx) and SHA256_OFF_STATE against alignof(uint32_t).
        lambda h, c, gh, gc: len(c["cast_regions_unaligned"]) == len(gc["cast_regions_unaligned"]),
        "convert funnel",
    ),
    (
        "no null-borrow test",
        "is the .c free of branches testing the borrow for null?",
        # A borrow comes from the arena, so the branch is dead on every call. The golden's answer is
        # zero and it earns it: sha256.c's two were deleted, along with 86 others across 33 files.
        # A test asserting a null borrow is refused is the stale half of that deletion - delete the
        # test, never restore the branch.
        lambda h, c, gh, gc: c["null_borrow_tests"] == gc["null_borrow_tests"],
        "convert unnull",
    ),
    (
        "guarded",
        "is the file wrapped in its include guard and its enable gate?",
        lambda h, c, gh, gc: h["gate_present"] == gh["gate_present"] and c["gate_present"] == gc["gate_present"],
        "convert shape",
    ),
    (
        "includes placed",
        "does only the config include sit above the gate, and everything else below?",
        lambda h, c, gh, gc: h["includes_above_gate"] == gh["includes_above_gate"]
        and c["includes_above_gate"] == gc["includes_above_gate"]
        and bool(h["includes_below_gate"]) == bool(gh["includes_below_gate"]),
        "convert shape",
    ),
    (
        "functions placed",
        "is every function in the .c static, below the gate, inside the decls?",
        lambda h, c, gh, gc: len(c["nonstatic_functions"]) == len(gc["nonstatic_functions"])
        and len(h["nonstatic_functions"]) == len(gh["nonstatic_functions"])
        and c["constructs_above_gate"] == gc["constructs_above_gate"],
        "convert shape",
    ),
    (
        "decls wrapped",
        "are the declarations inside PROTOCORE_BEGIN_DECLS / END_DECLS?",
        lambda h, c, gh, gc: h["counts"].get("decls_open", 0) == gh["counts"].get("decls_open", 0)
        and c["counts"].get("decls_open", 0) == gc["counts"].get("decls_open", 0),
        "convert shape",
    ),
]


# A check that cannot apply is not a check that failed. Two preconditions, both read off the module
# rather than listed per module:
#
#   no .c        - a header with no implementation beside it answers nothing about an implementation.
#   no borrow    - a module that names no PROTOCORE_<X>_BORROW carves none, and the three questions
#                  about how the borrow is carved have no subject. That is NOT the same as passing:
#                  they are reported n/a and counted separately, because a module that SHOULD own
#                  state and does not name a borrow is exactly what `convert gen` is for, and the
#                  namespace and operand checks above already catch it.
BORROW_CHECKS = ("borrow carved", "offsets chain", "assert covers last", "no local arrays")


def answers(h, c, gh, gc, ident=None):
    """Every check's answer for one module, in order: True yes, False no, None not applicable."""
    out = []
    for name, q, test, verb in CHECKS:
        if c is None or (name in BORROW_CHECKS and ident is not None and not ident.get("borrow")):
            out.append((name, None, verb))
            continue
        try:
            out.append((name, bool(test(h, c, gh, gc)), verb))
        except (TypeError, KeyError, IndexError):
            out.append((name, None, verb))
    return out


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    argv = sys.argv[1:]
    if not argv:
        argv = ["families"]  # the census is what a bare run is for
    cmd, rest = argv[0], argv[1:]

    if cmd == "design":
        for rel in rest:
            ident, h, c = design_of(rel)
            print(json.dumps({"identity": ident, "header": h, "source": c}, indent=2, default=str))
        return 0

    if cmd == "diff":
        gid, gh, gc = golden_designs()
        for rel in rest:
            ident, ds = diverge(rel, gid, gh, gc)
            print("%s  [%s]" % (rel, ident["module"]))
            if not ds:
                print("   golden")
            for where, k, g, v in ds:
                print("   %-7s %-28s golden=%-30s this=%s" % (where, k, g, v))
        return 0

    if cmd == "selfcheck":
        # A check the golden fails is a bug in the check, and it would mark all 400 modules
        # divergent on a question the target of the conversion does not itself answer yes to.
        gid, gh, gc = golden_designs()
        bad = [(n, q) for (n, ans, _), (_, q, _, _) in zip(answers(gh, gc, gh, gc), CHECKS) if ans is not True]
        for name, q, _, _ in CHECKS:
            ans = dict((n, a) for n, a, _ in answers(gh, gc, gh, gc))[name]
            print("   %-3s %-22s %s" % ("yes" if ans else "NO", name, q))
        print("\n%s: %d checks, %d the golden itself fails" % (GOLDEN, len(CHECKS), len(bad)))
        return 1 if bad else 0

    if cmd == "check":
        gid, gh, gc = golden_designs()
        for rel in rest:
            ident, h, c = design_of(rel)
            print("%s  [%s]" % (rel, ident["module"]))
            for name, ans, verb in answers(h, c, gh, gc):
                mark = "yes" if ans else ("---" if ans is None else "NO ")
                print("   %-3s %-22s %s" % (mark, name, "" if ans else "-> " + verb))
        return 0

    if cmd in ("families", "route"):
        roots = [a for a in rest if not a.startswith("-")] or ["src"]
        quiet = "--quiet" in rest
        gid, gh, gc = golden_designs()
        seen, _ = read_tree(roots)

        # A family is the ANSWER VECTOR: which questions this module says no to. Two modules in one
        # family fail the same set of checks, so one dry run stands for both - which is what makes
        # the sample a sample rather than a guess.
        fam, verbs = {}, {}
        na = {}
        for rel, ds, err, ident, h, c in seen:
            if err:
                fam.setdefault(("UNREADABLE",), []).append(rel)
                continue
            a = answers(h, c, gh, gc, ident)
            # n/a is NOT yes. Signing on the no-set alone puts a header with no .c - which answers
            # nothing about an implementation - in the same family as a module that answers yes to
            # every question, and the census then reports it as already golden.
            sig = tuple(n for n, ok, _ in a if ok is False)
            sig += tuple("%s (n/a)" % n for n, ok, _ in a if ok is None)
            fam.setdefault(sig, []).append(rel)
            for n, ok, v in a:
                if ok is False:
                    verbs.setdefault(n, v)
                elif ok is None:
                    na[n] = na.get(n, 0) + 1

        for sig, rels in sorted(fam.items(), key=lambda p: (-len(p[1]), p[0])):
            print("=" * 96)
            no = [n for n in sig if not n.endswith("(n/a)")]
            print(
                "%3d modules   %d no, %d n/a" % (len(rels), len(no), len(sig) - len(no))
                if sig
                else "%3d modules   answers YES to every check" % len(rels)
            )
            for n in no:
                print("      NO  %-22s -> %s" % (n, verbs.get(n, "")))
            for n in sig:
                if n.endswith("(n/a)"):
                    print("      n/a %s" % n[: -len(" (n/a)")])
            for rel in rels if not quiet else rels[:3]:
                print("         " + rel)
            if quiet and len(rels) > 3:
                print("         ... and %d more" % (len(rels) - 3))

        print("=" * 96)
        print("%d modules in %d families\n" % (len(seen), len(fam)))
        tally = {}
        for sig, rels in fam.items():
            for n in sig:
                tally[n] = tally.get(n, 0) + len(rels)
        print("%-24s %-16s %8s %8s" % ("check", "verb", "no", "n/a"))
        for name, q, _, verb in CHECKS:
            print("%-24s %-16s %8d %8d" % (name, verb, tally.get(name, 0), na.get(name, 0)))
        return 0

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
