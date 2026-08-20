"""One module converter: flat C module -> the sha256 golden shape.

Two phases so the inference is reviewable before it writes anything:

  goldenize.py scan <module.h>          print the spec it infers, as JSON
  goldenize.py gen  <spec.json>         write the header, restructure the .c, rewrite call sites
  goldenize.py gen  <spec.json> --dry   print the diff it would apply, and write nothing

--dry works on every writing subcommand (gen, shape, pimpl, funnel), so the output is read before
it lands rather than after.

The spec is small and hand-editable, so a module the scanner reads wrong is corrected by editing
one JSON file rather than by writing another script. Call-site rewriting goes through nsconv, which
is literal-aware, hoists to the statement (not the line), and refuses loop conditions.
"""

import difflib, io, json, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsconv as N
from codemask import code_mask
from funnel import find_context, funnel

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# --dry: every write goes through emit(), which prints the diff and leaves the file alone. The
# reads that follow still see the originals, so a dry run is the whole conversion, shown.
DRY = False


def emit(path, text):
    """Write @p text to @p path, or under --dry print the unified diff it would apply.

    C goes through clang-format on the way out, so the diff shown is the diff that lands and a
    generated line is laid out the same as a hand-written one.
    """
    if path.endswith((".c", ".h")):
        text = formatted(path, text)
    old = io.open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    if not DRY:
        io.open(path, "w", encoding="utf-8", newline="").write(text)
        return
    rel = os.path.relpath(path, R).replace("\\", "/")
    lines = list(difflib.unified_diff(old.splitlines(True), text.splitlines(True), "a/" + rel, "b/" + rel))
    if not lines:
        print("   unchanged: " + rel)
        return
    for line in lines:
        sys.stdout.write(line if line.endswith("\n") else line + "\n")


CLANG_FORMAT = os.environ.get("CLANG_FORMAT", "clang-format")


def formatted(path, text):
    """The text as .clang-format lays it out. Unformatted text on a format failure, reported.

    The pipe is UTF-8 both ways, stated rather than inherited: the platform default is cp1252 on
    Windows, and a source file holding a character outside it (an arrow in a comment) raised
    UnicodeEncodeError mid-run, after the header and source had already been written."""
    try:
        r = subprocess.run(
            [CLANG_FORMAT, "--assume-filename=" + path],
            input=text,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="surrogateescape",
            cwd=R,
        )
    except OSError as ex:
        print("   NOTE clang-format did not run (%s); output is unformatted" % ex)
        return text
    if r.returncode != 0:
        print("   NOTE clang-format refused %s: %s" % (os.path.basename(path), r.stderr.strip()))
        return text
    return r.stdout


DECL = re.compile(
    r"^(?P<ret>(?:const\s+)?[A-Za-z_][\w\s]*[\s\*]+)(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{]*)\)\s*;",
    re.M,
)

# A width, as a member name, for when two entries want the same one at different types. Keyed by the
# return type so the name says what it holds.
WIDTH_MEMBER = {
    "uint8_t": "u8",
    "uint16_t": "u16",
    "uint32_t": "u32",
    "uint64_t": "u64",
    "int8_t": "i8",
    "int16_t": "i16",
    "int32_t": "i32",
    "int64_t": "i64",
    "size_t": "n",
    "float": "f32",
    "double": "f64",
}


def resolve_result_collisions(entries, notes):
    """One member name per type. Two entries that landed on the same name at DIFFERENT types would
    share one declaration, so the wider one is silently truncated to the narrower - a get_word
    reporting 0x1234 as 0x34. The second and later types get a width-derived name instead.
    """
    seen = {}  # member -> the type that claimed it
    for e in entries:
        r = e.get("result")
        if not r:
            continue
        rtype = e["result_type"]
        if seen.setdefault(r, rtype) == rtype:
            continue
        alt = WIDTH_MEMBER.get(rtype)
        # stderr, because scan's stdout is the spec JSON and a caller pipes it.
        if not alt or seen.get(alt, rtype) != rtype:
            notes.append("%s returns %s and cannot share the '%s' member; name it in the spec" % (e["entry"], rtype, r))
            print("   NOTE " + notes[-1], file=sys.stderr)
            continue
        notes.append(
            "%s returns %s, so it reports on '%s' rather than sharing '%s' with a %s"
            % (e["entry"], rtype, alt, r, seen[r])
        )
        print("   NOTE " + notes[-1], file=sys.stderr)
        e["result"] = alt
        seen[alt] = rtype
    flag_unit_results(entries, notes)
    return entries


# member names that state a unit rather than a width, so they only read correctly on a module whose
# value carries that unit
UNIT_MEMBER = {"ms": "milliseconds"}


def flag_unit_results(entries, notes):
    """Report a default result member that names a unit. RESULT maps uint32_t to 'ms', which reads as
    milliseconds on every uint32_t an entry returns, CRCs and counts included.
    """
    for e in entries:
        unit = UNIT_MEMBER.get(e.get("result"))
        if unit:
            notes.append(
                "%s reports on '%s', which names %s; set it in the spec if it holds something else"
                % (e["entry"], e["result"], unit)
            )
            print("   NOTE " + notes[-1], file=sys.stderr)


# a return type -> the namespace member its value lands in
RESULT = {
    "void": None,
    "proto_bool": "ok",
    "int": "n",
    "size_t": "n",
    "uint32_t": "ms",
    "uint16_t": "value",
    "const char *": "text",
}


# widths and shapes protocore_config.h already brings in; anything else a parameter names has to be
# forward-declared, or the golden header would need a sibling include to see it.
BUILTIN = {
    "void",
    "char",
    "int",
    "long",
    "short",
    "float",
    "double",
    "unsigned",
    "signed",
    "size_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "proto_bool",
    "protocore_span",
    "uintptr_t",
    "intptr_t",
    "ptrdiff_t",
}


def defining_header(name):
    """The header under src/ that defines @p name as a typedef, or "" if none does.

    A `struct X;` declaration only names the same type when X is a struct TAG. CanFrame is a typedef
    of an anonymous struct, so `struct CanFrame *` is a different, incomplete type, and every args
    member declared that way took the wrong pointer - a warning under gcc, an error where the
    benches build with -Werror.
    """
    pat = re.compile(r"\}\s*%s\s*;|typedef\s+[\w ]*\b%s\b\s*;" % (re.escape(name), re.escape(name)))
    for base, _dirs, files in os.walk(os.path.join(R, "src")):
        for f in files:
            if not f.endswith(".h"):
                continue
            p = os.path.join(base, f)
            if pat.search(io.open(p, encoding="utf-8", errors="ignore").read()):
                return os.path.relpath(p, os.path.join(R, "src")).replace("\\", "/")
    return ""


def foreign_types(spec):
    """Base type names a parameter points at that neither the config nor this header supplies."""
    own = set()
    for t in spec.get("types", []):
        m = re.search(r"\}\s*(\w+)\s*;\s*$", t) or re.search(r"\(\*(\w+)\)", t) or re.search(r"(\w+)\s*;\s*$", t)
        if m:
            own.add(m.group(1))
    out = []
    for e in spec["entries"]:
        for p in e["params"]:
            if "*" not in p["type"]:
                continue  # a by-value parameter needs the complete type, not a declaration
            t = p["type"].replace("const", " ").replace("*", " ").strip()
            t = re.sub(r"\s+", " ", t)
            if not t or t in BUILTIN or t in out or t in own or " " in t:
                continue
            out.append(t)
    return out


TYPEDEF = re.compile(
    r"(?:^/\*\*(?:(?!\*/).)*?\*/\s*)?^typedef\s+(?:struct|enum|union)?[^;{]*\{.*?\}\s*\w+\s*;"
    r"|(?:^/\*\*(?:(?!\*/).)*?\*/\s*)?^typedef\s+[^;{]+;",
    re.M | re.S,
)


def typedef_end(s, brace):
    """Index just past the `} name;` that closes the typedef whose body opens at s[brace]."""
    i, depth = brace, 0
    while i < len(s):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                j = s.find(";", i)
                return j + 1 if j != -1 else i + 1
        i += 1
    return len(s)


COND = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[ \t]*([^\n]*)$", re.M)


def entry_arms(spec):
    """The capability `#if` each entry is DEFINED under, by entry name.

    An entry that only exists on one arm - promisc's begin/set_channel/end behind the vendor WiFi
    driver, modbus's rx behind the net stack - must be gated the same way where it is DECLARED and
    where it is INSTALLED, or the initializer names a function that arm did not compile. Read off
    the original .c, before conversion, so it is available when the header is written.
    """
    p = os.path.join(R, spec["source"].replace("/", os.sep))
    if not os.path.exists(p):
        return {}
    s = io.open(p, encoding="utf-8", errors="replace").read()
    gate = spec.get("gate", "")
    stack, at = [], {}
    pos = 0
    for m in COND.finditer(s):
        kind, expr = m.group(1), m.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            stack.append(expr if kind == "if" else ("defined(%s)" % expr))
        elif kind == "elif":
            if stack:
                stack[-1] = expr
        elif kind == "else":
            if stack:
                stack[-1] = "!(%s)" % stack[-1]
        elif kind == "endif":
            if stack:
                stack.pop()
        at[m.end()] = list(stack)
        pos = m.end()
    marks = sorted(at)

    def stack_at(i):
        lo, hi, best = 0, len(marks) - 1, None
        while lo <= hi:
            mid = (lo + hi) // 2
            if marks[mid] <= i:
                best, lo = marks[mid], mid + 1
            else:
                hi = mid - 1
        return at.get(best, [])

    out = {}
    for e in spec["entries"]:
        defs = list(re.finditer(r"^[A-Za-z_][^\n;]*?\b%s\s*\([^;{]*\)\s*\{" % re.escape(e["flat"]), s, re.M))
        # Two definitions means two arms - a capability one and its complement - and the entry
        # exists in every build. Only a SINGLE definition sitting under a capability is gated;
        # gating a two-arm entry deleted promisc's begin/set_channel/end from the host build that
        # has stubs for exactly that case.
        if len(defs) != 1:
            continue
        conds = [c for c in stack_at(defs[0].start()) if c and c != gate and "PROTOCORE" in c]
        if conds:
            out[e["entry"]] = conds[-1]
    _ = pos
    return out


def brace_end(s, brace):
    """Index just past the `}` matching the one at s[brace].

    A function body ends there and takes no `;`, so typedef_end's reach for the next semicolon
    runs into whatever follows - which for a run of `static inline` helpers is the NEXT one's
    body, captured half way and re-emitted nested inside the first.
    """
    i, depth = brace, 0
    while i < len(s):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(s)


def module_types(header_text):
    """The type vocabulary the module's own header defined: callbacks, enums, public structs.

    Regenerating the header must not delete these - they are the module's language, not its call
    surface. A storage struct that belongs in the .c is moved by naming it in the spec.

    The body is brace-matched rather than taken up to the first `} name;`: a struct with a nested
    anonymous struct closes that one first, and stopping there captured `} lru;` as though it were
    a whole type and left the rest of the struct behind.
    """
    out, mask = [], code_mask(header_text)
    taken = []  # conditional blocks kept whole, so their other arms are not emitted again
    guard = re.search(r"#ifndef (\w+)", header_text)
    skip = (guard.group(1) if guard else "\0", "PROTOCORE_ENABLE_", "PROTOCORE_NEED_")
    # A tagged enum or struct at file scope is the module's vocabulary too, and is not a typedef:
    # ptp.h's `enum protocore_ptp_msg_type { PROTOCORE_PTP_SYNC = 0x0, ... };` was dropped, and the
    # .c that compares against those names stopped compiling.
    for m in re.finditer(
        r"(?:^/\*\*(?:(?!\*/).)*?\*/\s*)?^(?:typedef\b|(?:enum|struct|union)\s+\w+\s*\{)", header_text, re.M | re.S
    ):
        if not mask[m.end() - 1]:
            continue
        if any(a <= m.start() < b for a, b in taken):
            continue  # another arm of a chain already kept whole
        semi = header_text.find(";", m.end())
        # A tagged form matches through its own `{`; a typedef's brace is still ahead.
        brace = m.end() - 1 if header_text[m.end() - 1] == "{" else header_text.find("{", m.end())
        end = typedef_end(header_text, brace) if brace != -1 and (semi == -1 or brace < semi) else semi + 1
        t = header_text[m.start() : end].strip()
        if re.search(r"\}\s*\w+(Args|Ns|Bind)\s*;\s*$", t):
            continue  # generated shapes, not the module's own
        # A type the preprocessor CHOOSES is one arm of a choice, exactly as a macro can be:
        # rawmemcpy selects proto_mv_word between uint64_t, uint32_t and uint16_t on PROTO_RAW_WORD.
        # Emitting the arms unconditionally redefines the type two ways and the file stops compiling.
        cond = enclosing_conditional(header_text, m.start(), skip)
        if cond:
            taken.append(cond)
            head = comment_above(header_text, cond[0])
            out.append(((head + "\n") if head else "") + header_text[cond[0] : cond[1]].strip())
            continue
        out.append(t)
    return out


def module_externs(header_text, ns):
    """The data the header published beside its calls: `extern const uint8_t x[32];` and its comment.

    rsa.h and bignum.h are the shape - a golden module exports its namespace AND the constants a
    caller compares against. Regenerating without them dropped tls13_msg's
    `extern const uint8_t protocore_tls13_hrr_random[32];` while leaving the .c that defines it and
    the parse that reads it, so the module and every consumer stopped compiling. The namespace's own
    `extern <X>Ns <X>;` is not one of these: the generator writes that itself.
    """
    out, mask = [], code_mask(header_text)
    for m in re.finditer(r"^extern\s+(?:const\s+)?(\w+)\b[^;{]*;", header_text, re.M):
        # The namespace's own declaration is the generator's to write, and the module spells it
        # `extern const <X>Ns <X>;` today - skipping only the unqualified form carried it here as
        # well, so the regenerated header declared HttpRoutes twice, once still const.
        if m.group(1) == ns or not mask[m.start()]:
            continue
        doc = comment_above(header_text, m.start())
        out.append(((doc + "\n") if doc else "") + m.group(0).strip())
    return out


def module_inlines(header_text):
    """The `static inline` helpers the header defined, with the comment above each.

    These emit no external symbol, so the one-symbol rule still holds and they belong in the
    regenerated header. Regenerating without them deleted six one-line Statusword predicates out of
    cia402.h, which showed up only as a link error from the suite that called them.
    """
    out, mask = [], code_mask(header_text)
    # PROTOCORE_INLINE is `static inline` behind the always_inline attribute
    # (config/platform/compiler_directives.h), and matching only the spelled-out form dropped
    # json.h's protocore_json_ok / _length / _c_str while eight call sites still used them.
    for m in re.finditer(r"^(?:static\s+inline|PROTOCORE_INLINE)\b", header_text, re.M):
        if not mask[m.start()]:
            continue
        brace = header_text.find("{", m.end())
        if brace == -1:
            continue
        end = brace_end(header_text, brace)
        doc = comment_above(header_text, m.start())
        out.append(((doc + "\n") if doc else "") + header_text[m.start() : end].strip())
    return out


# just the directive and its line continuations. The doc block above it is taken by doc_above, which
# anchors on the */ directly above: re.DOTALL here swallowed everything between blocks.
#
# A line is "anything but a newline, and a backslash only when a newline does not follow it". Plain
# [^\n]* is greedy and swallows the trailing backslash of a CONTINUED macro, so the continuation
# group never matches and the macro is captured as its first line alone - emitted with an empty
# body. NTLMSSP_CLIENT_DEFAULT_FLAGS went out of ntlmssp.h that way, and the name survives, so
# dropped_names cannot see it either.
_MLINE = r"(?:[^\n\\]|\\(?!\n))*"
MACRO = re.compile(r"^#[ \t]*define[ \t]+(\w+)%s(?:\\\n%s)*" % (_MLINE, _MLINE), re.M)


def comment_above(s, at):
    """The comment immediately above the line starting at @p at: a /** */ block, or a run of //.

    A table of wire values is usually introduced once for the whole group rather than per entry, so
    the // form matters as much as the doxygen one - taking only the latter drops the sentence that
    says what the table IS.
    """
    doc = doc_above(s, at)
    if doc:
        return doc
    lines = []
    i = N.line_start(s, at)
    while i > 0:
        prev_end = i - 1
        # A doxygen block can sit above the // run - one names the group, the other explains it.
        if s[:prev_end].rstrip().endswith("*/"):
            block = doc_above(s, prev_end)
            if block:
                lines.insert(0, block)
            break
        prev = N.line_start(s, prev_end)
        text = s[prev:prev_end]
        if not text.lstrip().startswith("//"):
            break
        lines.insert(0, text.rstrip())
        i = prev
    return "\n".join(lines)


def override_block(text, name, start, end):
    """Widen a #define to the `#ifndef NAME ... #endif` that makes it overridable.

    Returns the widened (start, end). A define not written that way comes back unchanged.
    """
    head = text.rfind("\n", 0, start) + 1
    prev = text.rfind("\n", 0, head - 1) + 1 if head else 0
    line = text[prev : head - 1] if head else ""
    if line.strip() != "#ifndef %s" % name:
        return start, end
    close = text.find("\n#endif", end)
    if close < 0:
        return start, end
    stop = text.find("\n", close + 1)
    return prev, (stop if stop >= 0 else len(text))


COND_OPEN = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef)\b", re.M)
COND_ANY = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b", re.M)


def enclosing_conditional(text, pos, skip=()):
    """The innermost `#if ... #endif` containing @p pos, as (start, end), or None.

    A define selected by the preprocessor is not one define, it is one ARM of a choice: rawmemcpy
    picks PROTO_RAW_WORD from PROTO_WORD_BITS across an #if / #elif / #elif chain. Collecting the
    arms as three separate defines drops the chain, emits all three unconditionally, and the last
    one wins - so a 64-bit part silently moves two bytes at a time and every width in the file is
    the wrong one. The block is kept whole instead.

    Blocks whose opening line matches @p skip are stepped over, which is how the include guard and
    the enable gate - both of which span the whole file - do not swallow every macro in it.

    A bare `#ifndef NAME` is NOT treated as a choice, whatever it wraps. That spelling is the
    overridable-constant idiom, and override_block already widens it when the name matches the
    define; widening it here as well would take `#ifndef PROTOCORE_OTHER` around an unrelated
    define and glue the two together.
    """
    opens = []
    for m in COND_ANY.finditer(text):
        line = text[m.start() : text.find("\n", m.start())]
        word = m.group(1)
        if word in ("if", "ifdef", "ifndef"):
            opens.append((m.start(), line, word))
        elif word == "endif":
            if not opens:
                continue
            start, oline, oword = opens.pop()
            end = text.find("\n", m.start())
            end = len(text) if end < 0 else end + 1
            if oword == "ifndef":
                continue
            if start < pos < end and not any(s in oline for s in skip):
                return start, end
    return None


def module_macros(header_text, guard):
    """The constants the module's header published: lengths, caps, wire values - each contiguous run
    kept as one block, with the comment that introduces it.

    Regenerating the header must not delete these either: a consumer sizes its buffers with them,
    and reads what they mean off the comment above them. A run is kept together because a table of
    wire values is one thing, not five - splitting it and spacing the parts out loses that it was a
    table at all. The include guard and anything the config owns are skipped.
    """
    runs, ends, prev_end = [], [], None
    taken = []  # conditional blocks already emitted whole, so their other arms are not re-emitted
    skip = (guard, "PROTOCORE_ENABLE_", "PROTOCORE_NEED_")
    for m in MACRO.finditer(header_text):
        name = m.group(1)
        if name == guard or name.endswith("_BORROW"):
            prev_end = None  # a skipped define breaks the run
            continue
        if any(a <= m.start() < b for a, b in taken):
            continue  # another arm of a chain already kept whole
        # A define the preprocessor CHOOSES is one arm of a choice, not a standalone constant: the
        # whole `#if ... #endif` is kept, once, or the arms come out unconditional and the last wins.
        cond = enclosing_conditional(header_text, m.start(), skip)
        if cond:
            taken.append(cond)
            head = comment_above(header_text, cond[0])
            runs.append([head] if head else [])
            runs[-1].append(header_text[cond[0] : cond[1]].strip())
            ends[len(runs) - 1 :] = [cond[1]]
            prev_end = None  # a block ends the run: what follows is a different thing
            continue
        # Contiguous means nothing but ONE newline between this define and the last one kept: a
        # blank line is how the header says the next define is a different thing.
        start, end = override_block(header_text, name, m.start(), m.end())
        gap = header_text[prev_end:start] if prev_end is not None else None
        if gap is not None and gap.strip() == "" and gap.count("\n") <= 1:
            runs[-1].append(header_text[start:end].strip())
        else:
            head = comment_above(header_text, start)
            runs.append([head] if head else [])
            runs[-1].append(header_text[start:end].strip())
        ends[len(runs) - 1 :] = [end]
        prev_end = end
    return [group_closed("\n".join(r), header_text, e) for r, e in zip(runs, ends)]


# A doxygen group opener, and its closer on a line of its own: `///@{` ... `///@}`, `/** @} */`.
GROUP_OPEN = re.compile(r"@\{")
GROUP_CLOSE = re.compile(r"@\}")
GROUP_CLOSE_LINE = re.compile(r"\A\s*\n?[ \t]*((?:///|/\*\*|\*)[^\n]*@\}[^\n]*)")


def group_closed(run, header_text, end):
    """A run whose comment opened a doxygen group, with the group's closing line put back.

    comment_above takes the `/** @name ... */ ///@{` that introduces a table of wire values, and
    the run ends at its last #define - so the `///@}` on the next line is outside both and would
    be dropped. Every define after the regenerated header's group would then read as part of it.
    """
    if not GROUP_OPEN.search(run) or GROUP_CLOSE.search(run):
        return run
    m = GROUP_CLOSE_LINE.match(header_text[end:])
    return run + "\n" + (m.group(1) if m else "///@}")


# Which persistent end the module's own bytes come from. protocore_config.h states the rule beside
# PROTOCORE_QUIC_CONN_CTX_BORROW: key material takes the secure end, everything else the plaintext
# one. The scanner cannot read which a module holds, so it writes "secure" and the spec is edited.
POOLS = {
    "secure": {
        "call": "protocore_secure_persist_span",
        "include": '#include "mmgr/secure/secure.h"',
        "why": "// the persistent end this module's key material is taken from",
    },
    "plaintext": {
        "call": "protocore_plaintext_persist_span",
        "include": '#include "mmgr/plaintext/plaintext.h"',
        "why": "// the persistent end this module's state is taken from",
    },
}


def camel(mod):
    return "".join(p.capitalize() for p in re.split(r"[_\-]", mod) if p)


def snake(obj):
    """The namespace object as the flat name its borrow and its regions are spelled with.

    `SshAppServer` -> `ssh_app_server`. The file leaf is not unique across the tree - three modules
    are named server.c and two client.c - so a borrow named from the leaf collides in
    protocore_config.h, where one global namespace holds every constant. The object is the one name
    that is already unique, because it is the symbol the module exports.
    """
    return re.sub(r"(?<=[^A-Z_])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", obj).lower()


def parse_params(txt):
    out = []
    for a in N.split_args(txt):
        a = a.strip()
        if not a or a == "void":
            continue
        m = re.match(r"^(?P<type>.*?)(?P<name>\w+)\s*(?P<arr>\[[^\]]*\])?$", a)
        if not m:
            continue
        out.append({"type": m.group("type").strip(), "name": m.group("name"), "arr": m.group("arr") or ""})
    return out


def doc_above(s, at):
    """The doxygen block immediately above the declaration starting at @p at, or ''."""
    head = s[:at].rstrip()
    if not head.endswith("*/"):
        return ""
    start = head.rfind("/**")
    return head[start:] if start != -1 else ""


def doc_tags(block):
    """A doxygen block split into its @brief, its @param texts by name, and its @return.

    The prose a module already published is the prose the golden header keeps: a converted header
    that drops it trades a documented surface for an undocumented one.
    """
    body = re.sub(r"^\s*/\*\*|\*/\s*$", "", block)
    body = "\n".join(re.sub(r"^\s*\*\s?", "", ln).strip() for ln in body.splitlines())
    tags, params, brief, ret = re.split(r"(?m)^@", body)[1:], {}, "", ""
    for t in tags:
        t = re.sub(r"\s+", " ", t).strip()
        if t.startswith("brief "):
            brief = t[6:].strip()
        elif t.startswith("param "):
            rest = t[6:].strip()
            rest = re.sub(r"^\[[^\]]*\]\s*", "", rest)  # @param[out] name text
            name, _, text = rest.partition(" ")
            params[name] = text.strip()
        elif t.startswith("return"):
            ret = t.split(" ", 1)[1].strip() if " " in t else ""
    return brief, params, ret


def one_line(text, cap=92):
    """Doc prose as one clause, trimmed to fit a member comment."""
    text = re.sub(r"@[a-z]+\s+", "", re.sub(r"\s+", " ", text)).strip().rstrip(".")
    if len(text) > cap:
        text = text[:cap].rsplit(" ", 1)[0] + " ..."
    return text


def first_sentence(text):
    """A doc block's opening sentence, whole. The full stop must end a word and be followed by the
    start of another sentence or nothing, so "e.g." mid-clause does not end it."""
    text = re.sub(r"@[a-z]+\s+", "", re.sub(r"\s+", " ", text)).strip()
    m = re.search(r"(?<=[A-Za-z0-9)\]])\.(?=\s+[A-Z]|$)", text)
    return (text[: m.start()] if m else text).rstrip(".")


def lead_lower(text):
    """The clause with a lowercase first word, unless that word is a name or an initialism."""
    if not text or not text[0].isupper():
        return text
    first = text.split(" ", 1)[0]
    if first.isupper() or any(c.isupper() for c in first[1:]) or "_" in first:
        return text
    return text[0].lower() + text[1:]


def sentence(text, fallback):
    """A @brief as a sentence: the prose if there is any, and a full stop unless it was trimmed."""
    text = text or fallback
    return text if text.endswith("...") else text + "."


# An entry on the Ns shape: `void (*name)(struct XInternal *ctx);` inside the namespace struct.
# An entry already on the golden shape, used to tell "Ns" from "already done".
GOLDEN_ENTRY = re.compile(r"\(\*const \w+\)\(uint8_t \*restrict work\)")

NS_ENTRY = re.compile(r"^[ \t]*void[ \t]*\(\*(?:const[ \t]+)?(\w+)\)\([^)]*\w*Internal[ \t]*\*[^)]*\)[ \t]*;", re.M)
NS_HANDLE = re.compile(r"^[ \t]*struct[ \t]+\w+Internal[ \t]*\*[ \t]*internal[ \t]*;[^\n]*\n", re.M)


def ns_entries(header_text):
    """The entries an already-Ns header declares, in the order the namespace lists them."""
    out = []
    mask = code_mask(header_text)
    for m in NS_ENTRY.finditer(header_text):
        if not mask[m.start()]:
            continue
        out.append(m.group(1))
    return out


def find_suites(hpath, mod):
    """The suite directories that test this module, the mirror path first.

    A module is tested from the mirror of its own path - src/a/b/c.c from test/unit/src/a/b/test_c/,
    which `env add` enforces. Matching `test_<mod>` anywhere under test/ instead returns every
    module in the tree that ends in the same leaf: ssh/app/client picked up ssh/client's suite and
    protocols/transport's, and gen would regenerate an unrelated suite's runner and rewrite its call
    sites. The walk stays as the fallback for a suite that does not sit on the mirror yet, and is
    only reached when the mirror holds nothing.
    """
    rel_h = os.path.relpath(hpath, R).replace("\\", "/")
    mirror = os.path.join(R, "test", "unit", *os.path.dirname(rel_h).split("/"), "test_" + mod)
    if os.path.isdir(mirror):
        return [os.path.relpath(mirror, R).replace("\\", "/")]
    # a module whose directory is its own name is mirrored one level up: src/a/b/b.c -> test/unit/src/a/test_b
    parent = os.path.dirname(os.path.dirname(rel_h))
    up = os.path.join(R, "test", "unit", *parent.split("/"), "test_" + mod) if parent else ""
    if up and os.path.isdir(up):
        return [os.path.relpath(up, R).replace("\\", "/")]
    out = []
    for dp, dns, _ in os.walk(os.path.join(R, "test")):
        for d in dns:
            if d == "test_" + mod:
                out.append(os.path.relpath(os.path.join(dp, d), R).replace("\\", "/"))
    return out


def scan_ns(hpath):
    """The spec for a module already on the Ns shape: only the signatures and the handle change.

    The args and result members are kept verbatim, so no call site has to restate its operands -
    the conversion is the borrow, not a new call surface.
    """
    s = io.open(hpath, encoding="utf-8").read()
    mod = os.path.splitext(os.path.basename(hpath))[0]
    gate = find_gate(s)
    ns = re.search(r"\}\s*(\w+Ns)\s*;", s)
    obj = re.search(r"^\s*extern\s+(?:const\s+)?\w+Ns\s+(\w+)\s*;", s, re.M)
    cpath = hpath[:-1] + "c"
    csrc = io.open(cpath, encoding="utf-8").read() if os.path.exists(cpath) else ""
    suites = find_suites(hpath, mod)
    # The borrow, the region macros and the span accessor are named from the OBJECT, not the file
    # leaf: three modules are named server.c and two client.c, and protocore_config.h holds one
    # global namespace, so a leaf-named borrow collides with another module's.
    name = snake(obj.group(1)) if obj else mod
    return {
        "from": "ns",
        "module": name,
        "ns": ns.group(1) if ns else camel(mod) + "Ns",
        "object": obj.group(1) if obj else camel(mod),
        "gate": gate,
        "header": os.path.relpath(hpath, R).replace("\\", "/"),
        "source": os.path.relpath(hpath, R).replace("\\", "/")[:-1] + "c",
        "borrow": "PROTOCORE_%s_BORROW" % name.upper(),
        "pool": "secure",
        "suites": suites,
        "owns_state": ns_owns_state(csrc),
        "span": "%s_work" % name,
        "entries": [{"entry": e} for e in ns_entries(s)],
    }


def seat_defaults(spec, pre):
    """The lines that put a non-zero default in the carved region, and the members they name.

    A borrow arrives zeroed. `defaults` states a member and its value; `seed` states whole
    statements, for a default no assignment expresses - an array, a string. Both run once, where the
    region is carved, before any entry that reads it.
    """
    seat = "".join(
        "            %s_CTX(s_own.span)->%s = %s;\n" % (pre, k, v)
        for k, v in sorted((spec.get("defaults") or {}).items())
    )
    seat += "".join(
        "            %s\n" % st.replace("CTX->", "%s_CTX(s_own.span)->" % pre) for st in (spec.get("seed") or [])
    )
    if seat:
        seat = "            // A borrow arrives zeroed, and these do not start at zero.\n" + seat
    named = set(spec.get("defaults") or {})
    named |= {m for st in (spec.get("seed") or []) for m in re.findall(r"CTX->(\w+)", st)}
    return seat, named


def ns_owns_state(csrc):
    """Does an ns-stage module hold state.

    Two shapes hold it: a `struct <X>Storage` reached through the Internal handle, and a file-static
    `<X>Ctx` beside it. Either one is state that belongs in the borrow.
    """
    return bool(re.search(r"(?:struct\s+\w+Storage\b|\}\s*\w+Storage\s*;)", csrc)) or bool(find_context(csrc))


GATE_TOKEN = re.compile(r"PROTOCORE_(?:ENABLE|NEED|TLS|HAS)_\w+")


def find_gate(s):
    """The header's enable gate, as the whole `#if` condition.

    A compound condition is the gate as written: tls13_msg.h and key_schedule.h are
    `#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)`. Matching only
    a bare token after `#if` skipped that line and took the next `#if` in the file, which is an
    inner capability arm - tls13_msg would have been regenerated under PROTOCORE_ENABLE_PQC_KEX and
    would have vanished from every build without it. `#ifdef` / `#ifndef` do not match, so the
    include guard is not read as a gate.

    A header with NO gate returns "". The gate is the FIRST conditional after the include guard AND
    it closes only at the end of the header - both, because http_route opens with
    `#if PROTOCORE_ENABLE_WEBSOCKET` around one include and tcp with `#if PROTOCORE_NEED_CLIENT`
    around one declaration, and either read alone takes an inner arm for the module's gate.
    """
    m = re.search(r"^[ \t]*#[ \t]*(?:if\w*|endif)\b[^\n]*", s[guard_end(s) :], re.M)
    if not m or not re.match(r"^[ \t]*#[ \t]*if[ \t]", m.group(0)):
        return ""
    cond = re.sub(r"/\*.*?\*/", " ", m.group(0).split(None, 1)[-1].lstrip("if \t"))
    cond = re.sub(r"//.*$", "", cond).strip()
    if cond.endswith("\\") or not GATE_TOKEN.search(cond):
        return ""
    return cond if wraps_body(s, guard_end(s) + m.end()) else ""


def guard_end(s):
    """Just past the include guard's `#define`, where the header's own text begins."""
    m = re.search(r"^[ \t]*#[ \t]*ifndef[ \t]+(\w+)[ \t]*\n[ \t]*#[ \t]*define[ \t]+\1\b[^\n]*\n", s, re.M)
    return m.end() if m else 0


def wraps_body(s, at):
    """Does the `#if` ending at @p at close only at the end of the header.

    A module with no gate at all opens with an inner capability arm - http_route's
    `#if PROTOCORE_ENABLE_WEBSOCKET` around one include, tcp's `#if PROTOCORE_NEED_CLIENT` around
    one declaration. Taking the first condition found regenerates the whole module under that arm
    and it vanishes from every build without it. The module's own gate is the one whose `#endif` is
    the last thing before the include guard's.
    """
    depth = 0
    for m in re.finditer(r"^[ \t]*#[ \t]*(if\w*|endif)\b[^\n]*", s[at:], re.M):
        if m.group(1).startswith("if"):
            depth += 1
            continue
        if depth:
            depth -= 1
            continue
        # The gate's own #endif. Only the linkage close and the include guard's #endif may follow
        # it - a declaration after it is one this condition does not cover, so it is not the gate.
        rest = re.sub(r"/\*.*?\*/|//[^\n]*", " ", s[at + m.end() :], flags=re.S)
        rest = re.sub(r"^[ \t]*PROTOCORE_(?:BEGIN|END)_DECLS[ \t]*$", "", rest, flags=re.M)
        return re.sub(r"^[ \t]*#[ \t]*endif\b[^\n]*$", "", rest, count=1, flags=re.M).strip() == ""
    return False


def outside_conditionals(s, at, gate=None):
    """Back @p at out of any `#if` it sits inside, but never out of the module's OWN gate.

    A definition every build needs cannot sit under a capability gate: ConnPool's first entry is
    obs_bump, under PROTOCORE_ENABLE_OBSERVABILITY, and the accessor placed above it was compiled
    only with observability on. But the module's own enable gate is different - the whole file is
    inside it, its includes are inside it, and nothing outside it names this module at all. Backing
    out of THAT put the accessor above the include that declares the pool call it makes.
    """
    depth, tops = 0, []
    for m in re.finditer(r"^[ \t]*#[ \t]*(if\w*|endif)\b[^\n]*", s[:at], re.M):
        if m.group(1).startswith("if"):
            tops.append(m)
            depth += 1
        else:
            depth -= 1
            if tops:
                tops.pop()
            if depth < 0:  # an #endif whose #if is above the region scanned
                depth, tops = 0, []
    # outermost first; stop at the one that tests the module's own gate
    for m in tops:
        if gate and gate in m.group(0):
            continue
        return comment_start(s, m.start())
    return at


def comment_start(s, at):
    """Index of the top of the comment block written directly above s[at], or at itself.

    A comment sitting on the line above a function describes that function, so an insertion that
    lands on the signature leaves the comment describing whatever was inserted.
    """
    i = at
    while True:
        head = s.rfind("\n", 0, i - 1)
        line = s[head + 1 : i - 1] if head != -1 else s[: i - 1]
        t = line.strip()
        if t.startswith("//"):
            i = head + 1
            continue
        if t.endswith("*/"):
            k = s.rfind("/*", 0, i)
            if k == -1:
                return i
            j = s.rfind("\n", 0, k)
            if s[j + 1 : k].strip():
                return i
            i = j + 1
            continue
        return i


def rename_handle(s):
    """`ctx` becomes `work` where it is the handle, and stays where it is not.

    Two shapes only. The handle's own parameter, which the two arrow rewrites above have already
    respelled `uint8_t *ctx` and which a `,` or `)` closes. And a bare `ctx` handed straight on to a
    sibling entry, which is an argument between parentheses and nothing else.

    Everything else keeps the name. A blanket rename caught `void *ctx;` declared as a MEMBER of an
    unrelated struct - UdpBind's opaque handler context - and renamed the DECLARATION while every
    `b->ctx` reader kept the old name, because `>` guarded them. The same rewrites also produce
    `uint8_t *ctx;` where the handle rides on a marshal record as a member; that is a member too.
    """
    s = re.sub(r"(uint8_t \*(?:restrict )?)ctx(?=\s*[,)])", r"\1work", s)
    return re.sub(r"(?<=[(,])(\s*)ctx(\s*)(?=[),])", r"\1work\2", s)


def guard_borrow(s, pre):
    """Nothing. The borrow cannot be null, so an entry never checks it.

    Storage comes from the caller, the TU static_asserts that what it was handed covers its
    regions, and the arena sums every borrow before the program is built - so the pool being short
    is a build failure, not a run-time case. This used to write a null check into every entry that
    reached the context: 443 branches across 106 files that cannot be taken, one extra step on
    every call for a case that does not exist.
    """
    del pre
    return s


def void_work(s):
    """`(void)work;` for an entry that never reaches the borrow.

    An entry takes the borrow whether or not it reads it, so one that reads only the namespace
    leaves the parameter unmentioned. The cast says the parameter is unused on purpose.
    """
    mask = code_mask(s)
    out, at = [], 0
    for m in re.finditer(r"^(?:static )?void \w+\(uint8_t \*restrict work\)[ \t]*\r?\n\{", s, re.M):
        if not mask[m.start()]:
            continue
        brace = m.end() - 1
        end = brace_end(s, brace)
        body = s[brace + 1 : end]
        bmask = code_mask(body)
        if any(bmask[k.start()] for k in re.finditer(r"(?<![\w.>-])work(?![\w])", body)):
            continue
        out.append(s[at : brace + 1] + "\n    (void)work;")
        at = brace + 1
    return "".join(out) + s[at:]


def gen_ns(spec):
    """Rewrite an Ns module to the golden shape, in place. Header first, then the source."""
    obj, notes = spec["object"], []
    hp = os.path.join(R, spec["header"].replace("/", os.sep))
    cp = os.path.join(R, spec["source"].replace("/", os.sep))
    h = io.open(hp, encoding="utf-8").read()
    c = io.open(cp, encoding="utf-8").read()
    PRE = spec["module"].upper()

    # The handle is a storage pointer and a namespace pointer. A member beyond those two is state,
    # and it goes in the storage struct the borrow carries: removing the handle without moving it
    # first drops it.
    hs = re.search(r"^struct\s+\w+Internal\s*\n\{(.*?)\n\};", c, re.M | re.S)
    if hs:
        held = [m.group(1) for m in re.finditer(r"^\s*[^;/]*?\b(\w+)\s*;", hs.group(1), re.M)]
        extra = [x for x in held if x not in ("store", "ns")]
        if extra:
            notes.append(
                "REFUSED: the handle also holds %s. Move those into the storage struct the borrow\n"
                "  carries, rewrite `ctx->%s` to the context, then re-run."
                % (", ".join("`%s`" % x for x in extra), extra[0])
            )
            return notes

    # the entry members take the borrow, and take it const: the table is fixed at build time
    h2 = NS_ENTRY.sub(lambda m: "    void (*const %s)(uint8_t *restrict work);" % m.group(1), h)
    # the opaque handle is what the borrow replaces
    h2 = NS_HANDLE.sub("", h2)
    # and the forward declaration of it, with the doc comment that described it, if nothing else
    # names it. A comment carrying `@var <X>Ns::` documents the NAMESPACE, not the handle, and only
    # happens to sit above the declaration - taking it deleted the whole HttpNs member list.
    if not re.search(r"\bInternal\b", NS_ENTRY.sub("", h2)):

        def _drop_fwd(m):
            doc = m.group(1) or ""
            return doc if re.search(r"@var\s+\w+Ns::", doc) else ""

        h2 = re.sub(
            r"((?:^[ \t]*/\*\*(?:(?!\*/).)*?\*/[ \t]*\r?\n)?)" r"^[ \t]*struct\s+\w+Internal\s*;[^\n]*\n",
            _drop_fwd,
            h2,
            flags=re.M | re.S,
        )
    # the blank line the removed member left above the closing brace
    h2 = re.sub(r"\n[ \t]*\r?\n(\}\s*\w+Ns\s*;)", r"\n\1", h2)
    h2 = re.sub(r"^[ \t]*\* @var \w+Ns::internal[^\n]*\n", "", h2, flags=re.M)
    if spec.get("owns_state") and ("protocore_%s_span" % spec["module"]) not in h2:
        decl = (
            "\n/**\n"
            " * @brief The %s bytes this module's state lives in.\n"
            " *\n"
            " * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where\n"
            " * that borrow comes from. Taken once from the end of the pool, which no mark and no release\n"
            " * walks, so the state lasts the life of the program.\n"
            " *\n"
            " * @return the span, or NULL while the pool was short - which every entry refuses.\n"
            " */\n"
            "uint8_t *protocore_%s_span(void);\n"
        ) % (spec["borrow"], spec["module"])
        m = re.search(r"^extern\s+(?:const\s+)?\w+Ns\s+\w+\s*;[^\n]*\n", h2, re.M)
        if m:
            h2 = h2[: m.end()] + decl + h2[m.end() :]
    if h2 != h:
        emit(hp, h2)
    notes.append("header: %d entries take the borrow" % len(spec["entries"]))

    # The handle type IS the borrow now, wherever it is spelled: an entry's lone parameter, a
    # private helper that takes it alongside its operands, or a local a fixed-signature callback
    # binds because it has no parameter to take one.
    c2 = re.sub(r"struct\s+\w+Internal\s*\*restrict\s+(\w+)", r"uint8_t *restrict \1", c)
    c2 = re.sub(r"struct\s+\w+Internal\s*\*\s*(\w+)", r"uint8_t *\1", c2)
    # A callback with nowhere to take a borrow from reaches for the accessor, the same as any
    # other caller outside the module's entries.
    # The instance the module declared for its handle. Its address is taken in two shapes: bound to
    # a local, and handed straight to a helper as an argument.
    inst = re.search(r"static\s+(?:struct\s+)?\w+Internal\s+(s_\w+)\s*=", c)
    # A _Static_assert sized against a member of the storage instance outlives that instance, and
    # has to stay a constant expression, so it asks the type rather than an object.
    store = re.search(r"static\s+(?:struct\s+)?(\w+Storage)\s+(s_\w+)\s*[=;]", c)
    # How the storage type is spelled, read from the ORIGINAL source: `struct <X>Storage { ... };`
    # carries a tag, `typedef struct { ... } <X>Storage;` does not, and naming a tagless one
    # `struct <X>Storage` declares a different, incomplete type. Read after the rewrites below it
    # would find the `struct` this function itself injects and take a tagless module for a tagged one.
    storage_tagged = bool(re.search(r"struct\s+\w+Storage\b", c))
    if store:
        sname = ("struct " if storage_tagged else "") + store.group(1)
        c2 = re.sub(
            r"sizeof\(%s\.(\w+)\)" % re.escape(store.group(2)), r"sizeof(((%s *)0)->\1)" % sname, c2
        )
        # Whatever still names the instance reads the region instead. A caller here has no handle -
        # it is a callback with a fixed signature - so it asks the accessor for the borrow.
        c2 = re.sub(
            r"(?<![\w.>-])%s\.(?=\w)" % re.escape(store.group(2)),
            "%s_CTX(protocore_%s_span())->" % (PRE, spec["module"]),
            c2,
        )
    if inst:
        # The same read through the internal instance: its store hop is the context itself now.
        c2 = re.sub(
            r"(?<![\w.>-])%s\.store->" % re.escape(inst.group(1)),
            "%s_CTX(protocore_%s_span())->" % (PRE, spec["module"]),
            c2,
        )
        # and its ns hop is the namespace, reached by name. A function that is not an entry reads
        # the instance directly - `s_sshtr.ns->slot = i` - and the instance is deleted below, so
        # leaving these named an identifier that is gone.
        c2 = re.sub(r"(?<![\w.>-])%s\.ns->" % re.escape(inst.group(1)), "%s." % obj, c2)
    # Where the module's own file passes the handle on, it passes a borrow now. A module that holds
    # state has an accessor to ask; a stateless one has no accessor emitted at all, so it passes the
    # same nominal buffer its external callers pass - naming the accessor there would leave the file
    # calling a function nothing defines.
    own_borrow = "protocore_%s_span()" % spec["module"] if spec.get("owns_state") else spec["span"]
    if inst:
        c2 = re.sub(
            r"&%s\b(?=\s*[,)])" % re.escape(inst.group(1)),
            lambda m: (
                m.group(0)
                if re.search(r"\.internal\s*=\s*$", c2[max(0, m.start() - 32) : m.start()])
                else own_borrow
            ),
            c2,
        )
    c2 = re.sub(
        r"uint8_t \*(\w+)\s*=\s*&s_\w+\s*;",
        lambda m: "uint8_t *%s = %s;" % ("work" if m.group(1) == "ctx" else m.group(1), own_borrow),
        c2,
    )
    # the namespace is reached by name, and the state through the borrow
    # Anchored: `ctx` here is the entry's own handle, not a MEMBER named ctx. Unanchored, these
    # matched `m->ctx->store->x` on a marshal record and produced `m-><PRE>_CTX(work)->x`, a member
    # access on a macro. A record that carries the handle is converted by hand, and is reported.
    c2 = re.sub(r"(?<![\w.>-])ctx->ns->", "%s." % obj, c2)
    if spec.get("owns_state"):
        c2 = re.sub(r"(?<![\w.>-])ctx->store->", "%s_CTX(work)->" % PRE, c2)
    elif re.search(r"(?<![\w.>-])ctx->store->\w+", c2):
        # No context was found, so no <PRE>_CTX macro is emitted and rewriting these would name one
        # that does not exist. A `store` on a module that holds no state points at something const -
        # hex's is a shared digit table - and where it should be read from is the module's business.
        reads = sorted({m.group(0) for m in re.finditer(r"(?<![\w.>-])ctx->store->\w+", c2)})
        notes.append(
            "HAND: this module holds no state, so there is no %s_CTX to read through, but it has "
            "%s. A store on a stateless module points at something const; read it by its own name."
            % (PRE, ", ".join("`%s`" % x for x in reads))
        )

    # The same two handles taken whole rather than dereferenced. These run after the arrow rules
    # above so they cannot eat the prefix of a `ctx->store->x`, and the negative lookahead keeps
    # them off anything that is still a dereference.
    c2 = re.sub(r"(?<![\w.>-])ctx->ns(?!\s*->)", "&%s" % obj, c2)
    if spec.get("owns_state"):
        c2 = re.sub(r"(?<![\w.>-])ctx->store(?!\s*->)", "%s_CTX(work)" % PRE, c2)
    c2 = re.sub(r"^[ \t]*\(void\)ctx;[ \t]*\r?\n", "", c2, flags=re.M)
    c2 = rename_handle(c2)

    # A storage struct is written either way: `struct <X>Storage { ... };` with a tag, or
    # `typedef struct { ... } <X>Storage;` without one. Reading only the tagged spelling skipped the
    # carve for a tagless module while still emitting the accessor and the `<PRE>_CTX(work)` reads,
    # so the file named a macro nothing defined - ssh/client is the shape.
    stor = re.search(r"struct\s+(\w+Storage)\b", c) or re.search(r"\}\s*(\w+Storage)\s*;", c)
    if stor and spec.get("owns_state"):
        ctype = ("struct " if storage_tagged else "") + stor.group(1)
        # the storage struct is what the borrow is carved for
        block = (
            "// The caller's borrow, split: the context at its offset. One pointer arrives and every\n"
            "// region is that pointer plus a compile-time offset, so the assert below proves the span\n"
            "// covers them before anything runs.\n"
            "#define %s_OFF_CTX 0u\n"
            "static_assert(%s_OFF_CTX + sizeof(%s) <= %s,\n"
            '              "%s is short of the module context - raise it in protocore_config.h, which"\n'
            '              " sums it into its arena");\n\n'
            "// The region, at its offset in the caller's borrow.\n"
            "#define %s_CTX(w) ((%s *)(void *)((w) + %s_OFF_CTX))\n"
        ) % (PRE, PRE, ctype, spec["borrow"], spec["borrow"], PRE, ctype, PRE)
        m = re.search(r"^static\s+(?:struct\s+)?\w+Storage\s+\w+\s*;[ \t]*\r?\n", c2, re.M)
        init = re.search(r"^static\s+(?:struct\s+)?\w+Storage\s+(\w+)\s*=\s*\{(.*?)\};", c2, re.M | re.S)
        if init:
            # A borrow arrives zeroed, so whatever the initializer seeded is lost unless the spec
            # says what it was. Then the carve seats it, once, before any entry can read the region.
            seeded = set(re.findall(r"\.(\w+)\s*=", init.group(2)))
            # A member named by a raw seed statement is seated too, just not by assignment.
            stated = set(spec.get("defaults") or {})
            stated |= {m for st in (spec.get("seed") or []) for m in re.findall(r"CTX->(\w+)", st)}
            missing = sorted(seeded - stated)
            if missing:
                notes.append(
                    "REFUSED: %s seeds %s and a borrow arrives zeroed. State them in the spec as\n"
                    '  "defaults": {"%s": "<value>"} so the carve seats them, then re-run.'
                    % (init.group(1), ", ".join("`%s`" % x for x in missing), missing[0])
                )
                return notes
            c2 = c2[: init.start()] + ("static %s %s;" % (ctype, init.group(1))) + c2[init.end() :]
            notes.append("the initializer's defaults move to the carve: " + ", ".join(sorted(stated)))
            # the instance is the plain declaration now, and the search above ran before the strip
            m = re.search(r"^static\s+(?:struct\s+)?\w+Storage\s+\w+\s*;[ \t]*\r?\n", c2, re.M)
        if m:
            inst = re.match(r"^static\s+(?:struct\s+)?\w+Storage\s+(\w+)\s*;", c2[m.start() : m.end()])
            c2 = c2[: m.start()] + block + c2[m.end() :]
            notes.append("the storage struct is now the context the borrow carries")
            # A function that is not an entry has no borrow to take, so a direct read of the
            # instance is left naming an identifier the borrow replaced.
            left = (
                sorted({k.group(0) for k in re.finditer(r"(?<![\w.>-])%s\s*\.\s*\w+" % re.escape(inst.group(1)), c2)})
                if inst
                else []
            )
            for x in left:
                notes.append(
                    "HAND: `%s` is read outside an entry - that instance is the borrow now. "
                    "Read it through %s_CTX(protocore_%s_span())." % (x, PRE, spec["module"])
                )
        else:
            notes.append(
                "REFUSED: no `static %sStorage <name>;` to carve the borrow for - the "
                "instance is not in the shape this reads." % obj
            )
            return notes

    # the opaque handle and its instance are what the borrow replaces
    c2 = re.sub(r"/\*\*(?:(?!\*/).)*?\*/\s*\nstruct\s+\w+Internal\s*\n\{[^}]*\};\n", "", c2, flags=re.S)
    c2 = re.sub(r"^struct\s+\w+Internal\s*\n\{[^}]*\};[ \t]*\r?\n", "", c2, flags=re.M | re.S)
    c2 = re.sub(r"^static\s+struct\s+\w+Internal\s+\w+\s*=\s*\{[^}]*\};[ \t]*\r?\n", "", c2, flags=re.M | re.S)
    # The designated initializer may carry a trailing comma of its own, which a pattern anchored
    # straight on the closing brace does not reach: the member survived and named a deleted handle.
    c2 = re.sub(
        r",?[ \t]*(\r?\n)?[ \t]*\.internal\s*=\s*&\w+\s*,?(?=\s*\})",
        lambda m: m.group(1) if (m.group(1) and c2.rfind("#", 0, m.start()) > c2.rfind("\n", 0, m.start())) else "",
        c2,
    )

    c2 = drop_empty_else(c2)

    defined = "uint8_t *protocore_%s_span(void)\n{" % spec["module"]
    if spec.get("owns_state") and defined not in c2:
        pool = POOLS[spec.get("pool", "secure")]
        # A default that is not zero is seated here, where the region is carved: it runs once, and
        # every entry that reads the region runs after it.
        seat, _named = seat_defaults(spec, PRE)
        acc = (
            "\n// --- the program's shared state, beside the namespace not on it -------------\n\n"
            "// The one owned instance, private to this TU: the pointer to the bytes this module took for\n"
            "// itself. A caller that hands in its own borrow never reaches it.\n"
            "typedef struct\n{\n"
            "    uint8_t *span; ///< %s persistent bytes, or null while the pool was short\n"
            "} %sOwnCtx;\nstatic %sOwnCtx s_own;\n\n"
            "// Not an entry: an entry takes a borrow and this is where that borrow comes from.\n"
            "uint8_t *protocore_%s_span(void)\n{\n"
            "    if (s_own.span == NULL)\n    {\n"
            "        protocore_span sp = %s(%s);\n"
            "        if (span.ok(sp))\n        {\n            s_own.span = sp.buf;\n%s        }\n    }\n"
            "    return s_own.span; // null while the pool was short, which every entry refuses\n}\n\n"
        ) % (spec["borrow"], obj, obj, spec["module"], pool["call"], spec["borrow"], seat)
        # The carve is the anchor: the accessor dereferences <PRE>_CTX, so it belongs under whatever
        # condition defines it. Anchoring on the first entry instead put it outside inner gates like
        # PROTOCORE_HAS_NET_STACK, where the storage - and the macro - do not exist.
        carve = re.search(r"^#define %s_CTX\(w\)[^\n]*\n" % PRE, c2, re.M)
        if carve:
            c2 = c2[: carve.end()] + acc + c2[carve.end() :]
        else:
            first = re.search(r"^static void %s_\w+\(uint8_t \*restrict work\)" % spec["module"], c2, re.M)
            if not first:
                first = re.search(r"^static void \w+\(uint8_t \*restrict work\)", c2, re.M)
            if first:
                at = outside_conditionals(c2, comment_start(c2, first.start()), spec.get("gate"))
                c2 = c2[:at] + acc + c2[at:]
        if pool["include"] not in c2:
            k = c2.index("#include")
            c2 = c2[:k] + pool["include"] + " " + pool["why"] + "\n" + c2[k:]
        notes.append("the span accessor is where the borrow comes from")

    c2 = void_work(c2)
    if spec.get("owns_state"):
        c2 = guard_borrow(c2, PRE)

    # A `ctx` the two rules above do not reach is a handle in a shape this has not seen. It names an
    # identifier the function no longer has, so it is reported here rather than found at the build.
    # `<type> *ctx;` is a DECLARATION - a member of an unrelated struct, or a local a fixed-signature
    # callback recovers - not a use of the handle. IfaceRow's `void *ctx;` is one, and reporting it
    # every time teaches the operator to skim the notes.
    left = sorted(
        {
            m.group(0)
            for m in re.finditer(r"(?<![\w.>-])ctx(?:->\w+)?", c2)
            if not re.match(r"ctx\s*;", c2[m.start() :]) or not re.search(r"\*\s*$", c2[: m.start()])
        }
    )
    for x in left:
        notes.append(
            "HAND: `%s` survived the handle rewrite - it is not a parameter and not an "
            "argument. Give it the borrow by hand." % x
        )

    # A stateless module's own file now passes that nominal buffer, so the file has to hold one.
    if not spec.get("owns_state") and re.search(r"\b%s\b" % re.escape(spec["span"]), c2):
        c2 = declare_work(c2, spec)

    if c2 != c:
        emit(cp, c2)
    notes.append("source: handle reads rewritten to the namespace and the borrow")
    return notes


# A plain vtable member: `size_t (*decode)(const char *src, uint8_t *dst, size_t dst_cap);` inside
# the namespace struct. Neither a flat declaration nor the Internal-handle shape, so neither of the
# other two scanners sees it.
VTABLE_ENTRY = re.compile(
    r"^[ \t]*(?P<ret>(?:const\s+)?[A-Za-z_][\w\s]*?[\s\*]*)\(\*(?:const\s+)?(?P<name>\w+)\)"
    r"\s*\((?P<params>[^;]*)\)\s*;",
    re.M,
)

# What the .c binds each member to: `.decode = b64_decode,` in the one initializer.
#
# The trailing separator is optional at the END of the body. The initializer is captured as the text
# BETWEEN the braces, so its closing `}` is the delimiter and is not part of what is searched - and
# a last binding written without a trailing comma then had neither a `,` nor a `}` after it. That
# dropped the LAST entry of every vtable module silently: rawmemcpy came back with thirteen of its
# fourteen, and the missing one was `read`, the only entry the module also declares as a prototype.
VTABLE_BIND = re.compile(r"\.(?P<entry>\w+)\s*=\s*(?P<impl>\w+)\s*(?:[,}]|$)")


def scan_vtable(hpath):
    """The spec for a module whose namespace is a plain function-pointer table.

    Base64, Json, Cbor and the DTLS trio are this shape: the members carry real return types and
    real parameters rather than a handle, so `scan` (flat declarations) and `scan_ns` (the
    `struct <X>Internal *ctx` shape) both come back with nothing. The entries are built the same way
    the flat path builds them - args structs and a result member per return type - and each carries
    two extra names: `impl`, the static function the .c binds to that member, and `call`, the
    `<Obj>.<entry>` spelling a caller uses, which is what the call-site pass matches.
    """
    s = io.open(hpath, encoding="utf-8").read()
    mod = os.path.splitext(os.path.basename(hpath))[0]
    gate = find_gate(s)
    ns = re.search(r"\}\s*(\w+Ns)\s*;", s)
    # The object is declared either way round: `extern <X>Ns <obj>;` with the definition in the .c,
    # or - rawmemcpy's shape - `static const <X>Ns <obj> = { ... };` defined IN THE HEADER, which
    # gives every translation unit that includes it a private copy. The second spelling was not
    # recognised, so the module fell through to the flat scan, which sees only its one non-inline
    # prototype and would have regenerated a one-entry namespace over a fourteen-entry table.
    obj = re.search(r"^\s*extern\s+(?:const\s+)?\w+Ns\s+(\w+)\s*;", s, re.M) or re.search(
        r"^\s*static\s+(?:const\s+)?\w+Ns\s+(\w+)\b", s, re.M
    )
    ns_name = ns.group(1) if ns else camel(mod) + "Ns"
    obj_name = obj.group(1) if obj else camel(mod)
    cpath = hpath[:-1] + "c"
    csrc = io.open(cpath, encoding="utf-8").read() if os.path.exists(cpath) else ""

    # The namespace's own doc block documents its members as `@var <X>Ns::<name> <text>`, so that is
    # where an entry's brief comes from rather than a comment above the member.
    var_doc = {}
    for m in re.finditer(r"@var\s+%s::(\w+)\s+([^\n]*)" % re.escape(ns_name), s):
        var_doc[m.group(1)] = one_line(re.sub(r"\s+", " ", m.group(2)).strip(), 110)

    # The members, in the order the struct lists them - which is what a POSITIONAL initializer binds
    # by. Collected before the initializer is read so both spellings can be paired.
    mask = code_mask(s)
    members = [m for m in VTABLE_ENTRY.finditer(s) if mask[m.start()] and m.group("ret").strip()]

    # What the .c binds, so an entry knows which static function is its implementation. The tree's
    # convention is a designated initializer, but base64's predates it and binds by position.
    bind = {}
    # The initializer sits beside the definition, so it is looked for in the .c AND in the header:
    # a `static const <X>Ns <obj> = {...}` header table binds its members there, and searching only
    # the .c came back with no bindings at all.
    init = re.search(
        r"\b%s\s+%s\b[^=;]*=\s*\{(.*?)\};" % (re.escape(ns_name), re.escape(obj_name)), csrc, re.S
    ) or re.search(r"\b%s\s+%s\b[^=;]*=\s*\{(.*?)\};" % (re.escape(ns_name), re.escape(obj_name)), s, re.S)
    if init:
        body = init.group(1)
        if "." in body and VTABLE_BIND.search(body):
            for m in VTABLE_BIND.finditer(body):
                bind[m.group("entry")] = m.group("impl")
        else:
            names = [x.strip() for x in body.split(",") if x.strip()]
            for mem, impl in zip(members, names):
                if re.match(r"^\w+$", impl):
                    bind[mem.group("name")] = impl

    entries, notes = [], []
    for m in members:
        name = m.group("name")
        ret = re.sub(r"\s+", " ", m.group("ret")).strip()
        if name not in bind:
            continue  # a callback typedef, or a member the initializer does not bind
        result = RESULT.get(ret, "value")
        if ret not in RESULT and ret.endswith("*"):
            result = "ptr"
        rtype = "proto_bool" if result == "ok" else ret
        params = parse_params(m.group("params"))
        entries.append(
            {
                "result_type": rtype,
                "flat": bind[name],
                "impl": bind[name],
                "call": "%s.%s" % (obj_name, name),
                "entry": name,
                "ret": ret,
                "result": result,
                "brief": var_doc.get(name, ""),
                "returns": "",
                "params": params,
            }
        )
    name = snake(obj_name)
    moved, held = classify_includes(s)
    return {
        "from": "vtable",
        "macros": module_macros(s, re.search(r"#ifndef (\w+)", s).group(1) if re.search(r"#ifndef (\w+)", s) else ""),
        "types": module_types(s),
        "inlines": module_inlines(s),
        "externs": module_externs(s, ns_name),
        "moved_includes": moved,
        "held_includes": held,
        "module": name,
        "ns": ns_name,
        "object": obj_name,
        "gate": gate,
        "header": os.path.relpath(hpath, R).replace("\\", "/"),
        "source": os.path.relpath(hpath, R).replace("\\", "/")[:-1] + "c",
        "borrow": "PROTOCORE_%s_BORROW" % name.upper(),
        "pool": "secure",
        "suites": find_suites(hpath, mod),
        "owns_state": bool(find_context(csrc)),
        "span": "%s_work" % name,
        "brief": first_sentence(doc_tags(doc_above(s, s.find("#ifndef")))[0]),
        "entries": resolve_result_collisions(entries, notes),
        "notes": notes,
    }


def scan(hpath):
    s = io.open(hpath, encoding="utf-8").read()
    mod = os.path.splitext(os.path.basename(hpath))[0]
    # ENABLE_ is the usual spelling, NEED_ the one a module gated by "some caller wants it" uses.
    gate = find_gate(s)
    entries = []
    for m in DECL.finditer(s):
        ret = re.sub(r"\s+", " ", m.group("ret")).strip()
        name = m.group("name")
        if name.startswith(("PROTOCORE_", "_")):
            continue
        if m.group("ret").strip().startswith("typedef") or "(*" in m.group(0):
            continue  # a function-pointer typedef declares a shape, not an entry
        result = RESULT.get(ret, "value")
        if ret not in RESULT and ret.endswith("*"):
            result = "ptr"
        # The member's TYPE is the return type verbatim: mapping size_t onto an int member
        # narrowed a count, and mapping an enum onto uint16_t threw its name away.
        rtype = "proto_bool" if result == "ok" else ret
        brief, pdoc, rdoc = doc_tags(doc_above(s, m.start()))
        params = parse_params(m.group("params"))
        for p in params:
            p["doc"] = one_line(pdoc.get(p["name"], ""))
        entries.append(
            {
                "result_type": rtype,
                "flat": name,
                "entry": re.sub(r"^(protocore_)?%s_?" % mod, "", name) or name,
                "ret": ret,
                "result": result,
                "brief": one_line(brief, 110),
                "returns": one_line(rdoc, 110),
                "params": params,
            }
        )
    moved, held = classify_includes(s)
    suites = find_suites(hpath, mod)
    # A module with no file-static context holds nothing between calls, so it carves no borrow,
    # states none, and needs no span accessor - tls_policy is the shape. That is read off the .c
    # rather than chosen: the state is there or it is not.
    collide_notes = []
    cpath = hpath[:-1] + "c"
    csrc = io.open(cpath, encoding="utf-8").read() if os.path.exists(cpath) else ""
    return {
        "macros": module_macros(s, re.search(r"#ifndef (\w+)", s).group(1) if re.search(r"#ifndef (\w+)", s) else ""),
        "types": module_types(s),
        "inlines": module_inlines(s),
        "externs": module_externs(s, camel(mod) + "Ns"),
        "suites": suites,
        "moved_includes": moved,
        "module": mod,
        "ns": camel(mod) + "Ns",
        "object": camel(mod),
        "gate": gate,
        "header": os.path.relpath(hpath, R).replace("\\", "/"),
        "source": os.path.relpath(hpath, R).replace("\\", "/")[:-1] + "c",
        "borrow": "PROTOCORE_%s_BORROW" % mod.upper(),
        "pool": "secure",
        "held_includes": held,
        "owns_state": bool(find_context(csrc)),
        "brief": first_sentence(doc_tags(doc_above(s, s.find("#ifndef")))[0]),
        "entries": resolve_result_collisions(entries, collide_notes),
    }


def args_brief(e):
    """What an entry's args struct holds, named from the parameters the module already documented."""
    if len(e["params"]) == 1 and e["params"][0].get("doc"):
        return one_line("The " + e["params"][0]["doc"], 74)
    return one_line("What %s takes: %s" % (e["entry"], ", ".join(p["name"] for p in e["params"])), 60)


RESULT_DOC = {
    "ok": "a call's true/false outcome",
    "n": "the count a call reports",
    "ms": "the milliseconds a call reports",
    "text": "the string a call reports",
    "value": "the value a call reports",
    "ptr": "the pointer a call reports",
}


def ns_doc(spec, args_types, results):
    """The namespace's own doc block: the usage snippet, a @var per member, and the borrow note.

    Every line here is the module's own published prose, moved rather than invented: a @brief
    becomes the entry's @var, a @param becomes the args member's, a @return becomes the result
    member's. What the module never documented stays undocumented rather than being made up.
    """
    obj, ns = spec["object"], spec["ns"]
    first = spec["entries"][0] if spec["entries"] else None
    out = ["/**", " * @brief %s" % sentence(spec.get("brief"), obj), " *"]
    out.append(" * A caller sets the members a call takes, invokes it through ::%s with the bytes it runs" % obj)
    out.append(" * out of, and reads the outcome off the same handle.")
    if first:
        out.append(" *")
        for p in first["params"]:
            out.append(" *   %s.%s_args.%s = ...;" % (obj, first["entry"], p["name"]))
        out.append(" *   %s.%s(work);" % (obj, first["entry"]))
        if first["result"]:
            out.append(" *   // %s.%s is what the call reports" % (obj, first["result"]))
    out.append(" *")
    for e, an in args_types:
        out.append(" * @var %s::%s_args  %s" % (ns, e["entry"], lead_lower(args_brief(e).rstrip("."))))
    for r in ["ok"] + [x for x in results if x != "ok"]:
        doc = next((x.get("returns") for x in spec["entries"] if x["result"] == r and x.get("returns")), "")
        out.append(
            " * @var %s::%s  %s" % (ns, r, lead_lower(one_line(doc, 68)) or RESULT_DOC.get(r, "what a call reports"))
        )
    for e in spec["entries"]:
        out.append(" * @var %s::%s  %s" % (ns, e["entry"], lead_lower(one_line(e.get("brief") or e["entry"], 68))))
    out.append(" *")
    if spec.get("owns_state", True):
        out.append(" * @c work is %s bytes the CALLER took, at an address it knows. It arrives" % spec["borrow"])
        out.append(" * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are")
        out.append(" * carved is this module's and is never named here.")
    else:
        out.append(" * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing")
        out.append(" * between calls, so there is no state to keep and nothing to wipe. The parameter is there so")
        out.append(" * a caller drives every namespace the same way.")
    out.append(" */")
    return out


def require_gate(spec):
    """Refuse a spec with no enable gate rather than writing `#if` with nothing after it.

    The gate is read off the header, so an empty one means the pattern did not match what this
    module actually uses - j1939 is gated on PROTOCORE_NEED_J1939. Guessing produces a header that
    cannot preprocess, which is a worse answer than stopping here.
    """
    if not spec.get("gate"):
        raise SystemExit(
            "spec has no gate: the header's `#if PROTOCORE_..._<MOD>` was not recognized.\n"
            '  State it in the spec as "gate": "PROTOCORE_ENABLE_<MOD>" (or NEED) and run gen again.'
        )


def dropped_names(spec, original, regenerated):
    """Names the old header defined that the new one does not, other than the entries it converted.

    A regenerated header is assembled from the parts the tool recognizes, so a part it does not
    recognize is not rewritten, it is deleted. Six `static inline` predicates went that way out of
    cia402.h and the only sign was a link error. Reported by name, so the next one is seen here.
    """
    flat = {e["flat"] for e in spec["entries"]}
    # A #define, a function, or an extern OBJECT: `extern const char NTS_EXPORTER_LABEL[];` is
    # none of the first two, so it went out of nts.h unseen.
    defined = re.compile(
        r"^(?:#[ \t]*define[ \t]+(\w+)"
        r"|extern[^;()=]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*;"
        r"|.*?\b(\w+)\s*\([^;]*\)\s*(?:\{|;))",
        re.M,
    )
    out = []
    for m in defined.finditer(strip_comments(original)):
        name = m.group(1) or m.group(2) or m.group(3)
        if not name or name in flat or name in out:
            continue
        if not re.search(r"(?<![\w])%s(?![\w])" % re.escape(name), regenerated):
            out.append(name)
    return out


def gutted_macros(original, regenerated):
    """Macros that had a body in the old header and have none in the new one.

    dropped_names only asks whether a NAME survived, so a macro captured as its first line alone
    passes it: the name is there and the body is gone. `#define X` with nothing after it then
    expands to nothing at every call site, which is a syntax error where a value was wanted and
    silence where a flag was.
    """

    def bodies(s):
        out = {}
        for m in MACRO.finditer(strip_comments(s)):
            body = m.group(0)[m.end(1) - m.start(0) :]
            out[m.group(1)] = body.replace("\\\n", " ").strip()
        return out

    old, new = bodies(original), bodies(regenerated)
    return [k for k, v in old.items() if v and k in new and not new[k]]


TAG_DECL = re.compile(r"^[ \t]*(struct|union|enum)[ \t]+(\w+)[ \t]*;", re.M)


def dropped_tag_decls(original, regenerated):
    """Tags the old header declared, that the new one uses without declaring.

    `struct ProtoHandler;` is neither a #define nor a function, so dropped_names does not see it,
    and the tag still appears in the new header as the pointer member's type - which declares it
    with member scope instead of file scope. modbus.h carried one for the Layer 5 dispatch record.
    """
    have = {m.group(2) for m in TAG_DECL.finditer(strip_comments(regenerated))}
    out = []
    for m in TAG_DECL.finditer(strip_comments(original)):
        tag = m.group(2)
        if tag in have or tag in out:
            continue
        if re.search(r"\b%s\s+%s\b" % (m.group(1), re.escape(tag)), regenerated):
            out.append(tag)
    return out


def strip_comments(s):
    """The text with comments blanked, so a name mentioned only in prose does not count as defined."""
    mask = code_mask(s)
    return "".join(c if mask[i] or c == "\n" else " " for i, c in enumerate(s))


def gen_header(spec, original):
    """Rebuild the header: keep its file comment, replace everything inside the gate."""
    obj, ns = spec["object"], spec["ns"]
    lines = []
    for mdef in spec.get("macros", []):
        lines.append(mdef)
        lines.append("")
    for t in spec.get("types", []):
        if t not in spec.get("drop_types", []):
            lines.append(t)
            lines.append("")
    for t in spec.get("inlines", []):
        lines.append(t)
        lines.append("")
    for t in spec.get("externs", []):
        lines.append(t)
        lines.append("")
    for t in foreign_types(spec):
        # A typedef cannot be forward-declared, so its own header comes in; only a struct tag takes
        # the declaration.
        where = defining_header(t)
        if where:
            lines.append('#include "%s" // %s: the type a parameter points at' % (where, t))
        else:
            lines.append("/** @brief %s, as the caller already knows it. */" % t)
            lines.append("struct %s;" % t)
        lines.append("")
    args_types = []
    for e in spec["entries"]:
        if not e["params"]:
            continue
        an = "%s%sArgs" % (obj, camel(e["entry"]))
        args_types.append((e, an))
        lines.append("/** @brief %s */" % sentence(args_brief(e), "What %s takes" % e["entry"]))
        lines.append("typedef struct")
        lines.append("{")
        fw = foreign_types(spec)
        for p in e["params"]:
            t = p["type"]
            base = t.replace("const", " ").replace("*", " ").strip()
            # Only where the declaration above is a struct tag. Where the type came in by include it
            # is already spelled correctly, and prefixing `struct` would name a different type.
            if base in fw and not defining_header(base):
                t = t.replace(base, "struct " + base)
            # A parameter's array declarator is a pointer - `uint8_t out[2]` is `uint8_t *out`.
            # Copying the bound onto a struct member makes a real array, which cannot be assigned,
            # so every call site fails to compile. The bound goes in the comment, where it belongs.
            t = re.sub(r"\s+", " ", t).strip()
            bound = " %s bytes." % p["arr"].strip("[]") if p["arr"] and p["arr"].strip("[]") else ""
            if p["arr"]:
                t = t if t.endswith("*") else t + " *"
            doc = " ///< %s" % (p["doc"] + bound if p.get("doc") else bound.strip()) if (p.get("doc") or bound) else ""
            lines.append("    %s%s;%s" % (t if t.endswith("*") else t + " ", p["name"], doc))
        lines.append("} %s;" % an)
        lines.append("")

    results = []
    for e in spec["entries"]:
        if e["result"] and e["result"] not in results:
            results.append(e["result"])

    body = [""] + ns_doc(spec, args_types, results)
    body += ["typedef struct", "{"]
    for e, an in args_types:
        body.append("    %s %s_args;" % (an, e["entry"]))
    body.append("")
    body.append("    proto_bool ok;")
    for r in results:
        if r != "ok":
            t = next((x.get("result_type", "int") for x in spec["entries"] if x["result"] == r), "int")
            body.append("    %s %s;" % (t, r))
    body.append("")
    for e in spec["entries"]:
        body.append("    void (*const %s)(uint8_t *restrict work);" % e["entry"])
    body.append("} %s;" % ns)
    body.append("")
    body.append("/** @brief The one symbol this module exports. */")
    body.append("extern %s %s;" % (ns, obj))
    if spec.get("owns_state", True) and spec.get("span"):
        body.append("")
        body.append("/**")
        body.append(" * @brief The %s bytes this module's state lives in." % spec["borrow"])
        body.append(" *")
        body.append(" * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where")
        body.append(" * that borrow comes from. Taken once from the end of the pool, which no mark and no release")
        body.append(" * walks, so the state lasts the life of the program.")
        body.append(" *")
        body.append(" * @return the span, or NULL while the pool was short - which every entry refuses.")
        body.append(" */")
        body.append("uint8_t *%s;" % spec["span"].replace("()", "(void)"))

    if spec.get("owns_state", True):
        note = (
            "// %s - the bytes this module runs out of - is stated in protocore_config.h, which sums\n"
            "// it into its arena. A caller takes them once and passes the pointer to every call. How they\n"
            "// are carved is this module's and is never named here.\n\n" % spec["borrow"]
        )
    else:
        note = (
            "// This module holds nothing between calls, so it carves no borrow and states none. An entry\n"
            "// takes one all the same, and never reads it, so every namespace in the tree is invoked the\n"
            "// same way.\n\n"
        )

    i = original.find("#ifndef")
    head = original[:i] if i != -1 else original.split("*/")[0] + "*/\n\n"
    g = re.search(r"#ifndef (\w+)\n#define \1", original)
    guard = g.group(1) if g else "PROTOCORE_%s_H" % spec["module"].upper()
    return (
        head
        + "#ifndef %s\n#define %s\n\n" % (guard, guard)
        + '#include "protocore_config.h" // the entry point: protocore_types.h for the widths\n'
        # An include a kept type needs by value: the header cannot declare that type without it.
        + "".join(
            "#include %s // the complete type a public struct below holds by value\n" % h
            for h in spec.get("held_includes", [])
        )
        + "\n"
        + "#if %s\n\n" % spec["gate"]
        + "PROTOCORE_BEGIN_DECLS\n\n"
        + note
        + "\n".join(lines)
        + "\n".join(body)
        + "\n\nPROTOCORE_END_DECLS\n\n#endif // %s\n\n#endif // %s\n" % (spec["gate"], guard)
    )


# Rewrites one file may take before the pass is treated as non-terminating. The largest real count
# in this tree is json.c at 57.
REWRITE_CAP = 2000


def already_converted(args, span):
    """True when this call is the `<Obj>.<entry>(<span>)` a prior pass of this loop wrote.

    A vtable module is matched on the same spelling the conversion emits, and a successful rewrite
    restarts the scan from the top of the file. An entry taking exactly one parameter is therefore
    met again with exactly one argument, and without this it is staged a second time - forever.
    """
    return len(args) == 1 and args[0].strip() == span.strip()


def rewrite_calls(spec, roots=("src", "test", "examples", "vendor", "include")):
    """Every call to a flat name becomes staging + entry + the result member.

    A vtable module is already called through its namespace - `Base64.decode(a, b, c)` - so what a
    caller writes is `<Obj>.<entry>`, not the flat name the .c defines. The entry states that
    spelling as `call`, and it is what the pattern matches; the lookbehind that keeps a flat name off
    `x.name(` is dropped for it, since the dot IS the call here.
    """
    obj = spec["object"]
    vtable = spec.get("from") == "vtable"
    byname = {k: e for e in spec["entries"] for k in [(e.get("call") if vtable else e.get("flat"))] if k}
    lead = "" if vtable else r"(?<![\w.>])"
    total, skipped = 0, []
    # No flat names to look for means no call sites to rewrite. Guarded rather than assumed:
    # "|".join over an empty set is the EMPTY STRING, so the alternation becomes `()`, which matches
    # at every `(` in every file under every root, and the lookup below raises KeyError on ''. A
    # module already on the Ns shape scans to zero entries and hits exactly that.
    if not byname:
        return total, skipped
    pat = re.compile(r"%s(%s)\s*\(" % (lead, "|".join(re.escape(k) for k in byname)))
    for root in roots:
        for dp, _, fns in os.walk(os.path.join(R, root)):
            if ".pio" in dp:
                continue
            for fn in sorted(fns):
                if not fn.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, fn)
                rel = os.path.relpath(p, R).replace("\\", "/")
                if rel in (spec["header"], spec["source"]):
                    continue
                s = io.open(p, encoding="utf-8", errors="replace").read()
                if not pat.search(s):
                    continue
                at, n, before = 0, 0, len(skipped)
                mask = code_mask(s)
                while True:
                    m = pat.search(s, at)
                    if not m:
                        break
                    if not mask[m.start()]:
                        at = m.end()  # named inside a comment or a literal: prose, not a call
                        continue
                    e = byname[m.group(1)]
                    end = N.close_paren(s, m.end())
                    # A `)` followed by `{` is a DEFINITION, not a call. A module whose entry is
                    # defined in a sibling .c has one here, and rewriting it produced staging lines
                    # out of the parameter list and left the body dangling.
                    if re.match(r"\s*\{", s[end:]):
                        skipped.append(
                            (rel, s[: m.start()].count("\n") + 1, "definition, not a call - convert it by hand")
                        )
                        at = m.end()
                        continue
                    a = N.split_args(s[m.end() : end - 1])
                    if already_converted(a, spec["span"]):
                        at = m.end()
                        continue
                    if len(a) != len(e["params"]):
                        at = m.end()
                        continue
                    staging = [
                        "%s.%s_args.%s = %s;" % (obj, e["entry"], p_["name"], v) for p_, v in zip(e["params"], a)
                    ]
                    staging.append("%s.%s(%s);" % (obj, e["entry"], spec["span"]))
                    value = "%s.%s" % (obj, e["result"] or "ok")
                    try:
                        s = N.rewrite(s, m.start(), end, staging, value, pat, mask)
                        mask = code_mask(s)
                        n += 1
                        at = 0
                        if n > REWRITE_CAP:
                            raise SystemExit(
                                "rewrite_calls: %s took %d rewrites of %s, past the %d cap.\n"
                                "  A pass that keeps matching what it just wrote does not terminate;"
                                " the file was NOT written."
                                % (rel, n, m.group(1), REWRITE_CAP)
                            )
                    except ValueError as ex:
                        skipped.append((rel, s[: m.start()].count("\n") + 1, str(ex)))
                        at = m.end()
                # A file whose every call site was skipped still gets the include and the borrow: the
                # hand conversion those skips ask for names both, and a benchmark whose calls all sit
                # inside DBENCH_OP is exactly that file.
                if n or len(skipped) > before:
                    for inc in spec.get("moved_includes", []):
                        if inc not in s:
                            k = s.index("#include")
                            s = s[:k] + "#include %s\n" % inc + s[k:]
                    s = declare_work(s, spec)
                    print("   %-72s %d%s" % (rel, n, "" if n else "  (all skipped; borrow declared)"))
                    emit(p, s)
                    total += n
    return total, skipped


# A function definition at file scope: a signature ending in `)` with the brace on the next line,
# which is the shape clang-format leaves this tree in.
FUNC_DEF = re.compile(r"^([A-Za-z_][^;{}#]*?\([^;{}]*\))[ \t]*\r?\n\{", re.M | re.S)


def takes_work(s, pos):
    """Does the function containing @p pos have a `work` parameter of its own.

    Inside the module's own .c, an ENTRY has one and a private helper does not. Handing `work` to a
    helper that has none leaves the file naming an undeclared identifier - http.c's send_error_close,
    route matcher and poll helper were three.
    """
    sig = None
    for m in FUNC_DEF.finditer(s):
        if m.start() > pos:
            break
        sig = m.group(1)
    return bool(sig) and bool(re.search(r"\bwork\s*[,)]", sig))


def above_capability_split(s, pos):
    """Where the span accessor goes: inside the module's gate, but above any capability arm.

    A two-arm module carves its context in ONE arm, so the first entry definition - which is what
    the accessor is otherwise placed before - sits inside that arm. The accessor put there does not
    exist for the other arm, and the link fails on whichever arm the env actually builds:
    mdns_service's portable responder called a `protocore_mdns_service_span` that only the vendor
    arm defined. The outermost open `#if` at @p pos is the module's own gate and the accessor
    belongs under it; the next one in is a capability split and the accessor belongs above it.
    """
    open_ifs = []
    for m in re.finditer(r"^[ \t]*#[ \t]*(if\w*|endif)\b[^\n]*\n", s[:pos], re.M):
        if m.group(1).startswith("if"):
            open_ifs.append(m.start())
        elif open_ifs:
            open_ifs.pop()
    return open_ifs[1] if len(open_ifs) > 1 else pos


def rewrite_ns_calls(spec, roots=("src", "test", "examples", "vendor", "include")):
    """`X.entry(X.internal)` becomes `X.entry(work)` everywhere the object is driven.

    The operands are already on the namespace, so nothing is hoisted and nothing can be refused for
    ordering: this is a handle swap, not a call rebuild. What it cannot decide is WHICH borrow a
    caller outside the module holds, so a file that has none is reported rather than guessed at.
    """
    obj = spec["object"]
    entries = {e["entry"] for e in spec["entries"]}
    # ONLY this object, by name, or an alias the spec states for it. A child handle is real -
    # `Physical.radio->ps_set(Physical.radio->internal)` is the same call - but which expression
    # holds THIS object cannot be read off the text, and a pattern that matched any expression
    # rewrote every OTHER namespace in the files this pass touched.
    holders = [re.escape(obj)] + [re.escape(a) for a in spec.get("aliases", [])]
    call = re.compile(r"(?<![\w.])(%s)(\.|->)(\w+)\(\s*\1\2internal\s*\)" % "|".join(holders))
    span = "protocore_%s_span()" % spec["module"]
    total, skipped = 0, []
    for root in roots:
        for dp, _d, fns in os.walk(os.path.join(R, root)):
            if ".pio" in dp:
                continue
            for fn in sorted(fns):
                if not fn.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, fn)
                rel = os.path.relpath(p, R).replace("\\", "/")
                s = io.open(p, encoding="utf-8", errors="replace").read()
                if not call.search(s):
                    continue
                mask = code_mask(s)
                own = rel == spec["source"]
                out, n, at = [], 0, 0
                for m in call.finditer(s):
                    if not mask[m.start()] or m.group(3) not in entries:
                        continue
                    # ONLY inside the module's own .c is the `work` in scope THIS module's
                    # borrow. In any other file that `work` belongs to the module being compiled,
                    # and passing it on would have the callee carve its context out of someone
                    # else's bytes - promisc handing Radio its own borrow was exactly that.
                    if own and takes_work(s, m.start()):
                        arg = "work"
                    elif spec.get("owns_state"):
                        arg = span
                    else:
                        # Holds nothing, so there is no span to hand out: the caller declares the
                        # nominal borrow every entry takes and never reads.
                        arg = spec["span"]
                    out.append(s[at : m.start()] + "%s%s%s(%s)" % (m.group(1), m.group(2), m.group(3), arg))
                    at = m.end()
                    n += 1
                if n:
                    s = "".join(out) + s[at:]
                    if not spec.get("owns_state") and not own:
                        s = declare_work(s, spec)
                    emit(p, s)
                    print("   %-72s %d" % (rel, n))
                    total += n
    return total, skipped


def declare_work(s, spec):
    """A stateless module's callers hold the borrow themselves, so give the file one to pass.

    The module never reads it - it is there so a caller drives every namespace the same way - so
    the size is nominal and one per file is enough.
    """
    if spec.get("owns_state", True):
        return s
    name = spec["span"]
    if not re.match(r"^\w+$", name) or re.search(r"\b%s\s*\[" % re.escape(name), s):
        return s
    decl = "static uint8_t %s[16]; // the borrow an entry takes; %s never reads it\n\n" % (name, spec["object"])
    k = work_decl_at(s, spec["object"])
    return s[:k] + decl + s[k:]


def work_decl_at(s, obj):
    """Where the nominal borrow goes: with the calls that pass it, never outside their arm.

    The calls live inside the CONSUMING file's own gate - wamp.c reaches Json only under
    `#if PROTOCORE_ENABLE_WAMP` - so a declaration parked above that gate is 16 bytes of BSS in
    every build with the capability off. It must not go the other way either: parked beside an
    include under an INNER `#if CAP`, it vanishes when that capability is off and every call site
    names an identifier that is not there. So: the outermost conditional that encloses the first
    call, and after the last include inside it.
    """
    mask = code_mask(s)
    first = None
    for m in re.finditer(r"(?<![\w.>])%s\s*\." % re.escape(obj), s):
        if mask[m.start()]:
            first = m.start()
            break

    # The conditionals still open where that call sits, outermost first.
    open_ifs, stack = [], []
    for m in re.finditer(r"^[ \t]*#\s*(if\w*|endif)\b[^\n]*\n", s, re.M):
        if first is not None and m.start() >= first:
            break
        if m.group(1).startswith("if"):
            stack.append(m)
        elif stack:
            stack.pop()
    open_ifs = stack

    start = open_ifs[0].end() if open_ifs else 0
    stop = first if first is not None else len(s)
    depth, last = 0, None
    for m in re.finditer(r"^[ \t]*#\s*(if\w*|else|elif|endif|include)\b[^\n]*\n", s[start:stop], re.M):
        kind = m.group(1)
        if kind.startswith("if"):
            depth += 1
        elif kind == "endif":
            depth = max(0, depth - 1)
        elif kind == "include" and depth == 0:
            last = m
    k = start + last.end() if last else start
    while s[k : k + 1] == "\n":
        k += 1
    return k


def drop_flat_protos(s, flats):
    """Forward declarations of the flat names this conversion renamed, and the comment over them.

    A file that calls an entry above its definition carries `static <ret> <flat>(<params>);` near
    the top. The definition below it becomes `static void <mod>_<entry>(uint8_t *restrict work)`,
    so what is left declares a function that no longer exists and describes a rule that no longer
    applies. A block is dropped whole only when every declaration in it is one of these.
    """
    if not flats:
        return s
    run = re.compile(
        r"^(?:[ \t]*static\s[^;{]*?\b(?:%s)\s*\([^;{]*\);[ \t]*\n)+" % "|".join(re.escape(f) for f in flats), re.M
    )
    lead = re.compile(r"(?:^[ \t]*//[^\n]*\n)+\Z", re.M)
    after = re.compile(r"[ \t]*static\s[^;{]*\);[ \t]*$", re.M)
    out, at = [], 0
    for m in run.finditer(s):
        if m.start() < at:
            continue
        start, end = m.start(), m.end()
        # The comment introducing the run goes with it, unless a declaration this pass did not
        # rename follows - that one still needs what the comment says.
        c = lead.search(s, 0, start)
        if c and not after.match(s, end):
            start = c.start()
        out.append(s[at:start])
        at = end
        while s[at : at + 1] == "\n":
            at += 1
    return "".join(out) + s[at:] if out else s


def drop_self_assign(s, obj):
    """Take out `Obj.x = Obj.x;`, moving a trailing comment onto the call above it."""
    s = re.sub(
        r"(?m)^([ \t]*\w+\(work\);)[ \t]*\r?\n[ \t]*%s\.(\w+) = %s\.\2;[ \t]*(//[^\n]*?)[ \t]*\r?\n"
        % (re.escape(obj), re.escape(obj)),
        lambda m: "%s %s\n" % (m.group(1), m.group(3)),
        s,
    )
    return re.sub(r"(?m)^[ \t]*%s\.(\w+) = %s\.\1;[ \t]*\r?\n" % (re.escape(obj), re.escape(obj)), "", s)


def enclosing_has_work(s, pos):
    """True when the function the call at pos sits in takes a `work` parameter.

    An entry takes the borrow, so a self-call inside one passes it straight on. A module's own
    private helper does not, and `helper(work)` written there names an identifier that is not
    declared. profibus's three telegram parsers are that shape.
    """
    prev = s.rfind("\n}", 0, pos)  # a definition ends at a brace in column 1
    head = s[prev + 2 : pos] if prev >= 0 else s[:pos]
    m = None
    for m in re.finditer(r"^[\w][\w \t\*]*\s[\*]?\w+\s*\(([^;{)]*)\)\s*\{", head, re.M):
        pass  # the last one that opened is the one this call is inside
    return bool(m) and "work" in m.group(1)


def land_returns(body, obj, result):
    """`return X;` becomes the result member assigned, then a bare `return;` at the original
    indentation. The one at the very end of the body goes: falling off a void entry is the same
    thing. A comment it carried describes the value, so it rides up onto the assignment.

    Only a `return` that is CODE: prose in a comment says "the last one to return is the rightmost",
    and rewriting that spliced an assignment into the middle of a sentence. The keyword and its
    semicolon are found separately, both at code positions - matching the value as everything up to
    the first `;` let a `return` inside a comment swallow the real statement after it, which then
    went unconverted."""
    bmask = code_mask(body)
    spans = []
    for mm in re.finditer(r"\breturn\b", body):
        if not bmask[mm.start()]:
            continue
        j = mm.end()
        while j < len(body) and not (body[j] == ";" and bmask[j]):
            j += 1
        value = body[mm.end() : j].strip()
        # A comment after the `;` describes the value, so it travels with it onto the assignment
        # rather than staying behind on a bare `return;` that says nothing.
        tail = re.match(r"[ \t]*(//[^\n]*?)[ \t]*(?=\n)", body[j + 1 :])
        if j < len(body) and value:
            spans.append((mm.start(), j + 1 + (tail.end() if tail else 0), value, tail.group(1) if tail else ""))
    for a, b, value, note in reversed(spans):
        indent = re.match(r"[ \t]*", body[body.rfind("\n", 0, a) + 1 :]).group(0)
        lead = "%s.%s = %s;%s" % (obj, result, value, " " + note if note else "")
        body = body[:a] + "%s\n%sreturn;" % (lead, indent) + body[b:]
    mt = re.search(r"\n[ \t]*return;[ \t]*(//[^\n]*?)[ \t]*\n?\s*$", body)
    if mt:
        body = body[: mt.start()].rstrip() + " " + mt.group(1) + "\n"
    return re.sub(r"\n[ \t]*return;\s*$", "\n", body)


def drop_empty_else(text):
    """Drop an #else whose branch the conversion emptied.

    A two-arm module states its Internal instance once per arm, so deleting both leaves an #else
    with nothing under it. The branch must hold only whitespace, which is what keeps the pattern
    off a branch that still emits code.
    """
    return re.sub(r"^#else[ \t]*(?://[^\n]*)?\r?\n(?:[ \t]*\r?\n)*(?=#endif)", "", text, flags=re.M)


def drop_void_work(s):
    """`(void)work;` says the entry never touches the borrow. The funnel threads `work` into every
    entry that reaches the context, so the cast comes out of the ones where it stopped being true.
    A mention inside a comment does not count, and leaving the cast in place is the safe way to be
    wrong: removing it from an entry that really is done with `work` is an unused parameter."""
    mask = code_mask(s)
    cuts = []
    for m in re.finditer(r"\n\{\n[ \t]*\(void\)work;\n", s):
        if not mask[m.start() + 1]:
            continue
        i, depth = m.start() + 2, 1
        while i < len(s) and depth:
            if mask[i]:
                if s[i] == "{":
                    depth += 1
                elif s[i] == "}":
                    depth -= 1
            i += 1
        body, at = s[m.end() : i - 1], m.end()
        if any(mask[at + x.start()] for x in re.finditer(r"\bwork\b", body)):
            cuts.append((m.start() + 3, m.end()))
    for a, b in reversed(cuts):
        s = s[:a] + s[b:]
    return s


def restructure_source(spec):
    """Turn each flat definition into an entry over the borrow, and append the Ns definition.

    Refuses (and reports) anything it cannot do faithfully, rather than guessing: an unmatched
    definition, or a `return` whose value cannot be placed on a result member.
    """
    obj, ns = spec["object"], spec["ns"]
    p = os.path.join(R, spec["source"].replace("/", os.sep))
    s = io.open(p, encoding="utf-8").read()
    # what the header used to include, the .c now includes itself
    for inc in spec.get("moved_includes", []):
        if inc not in s:
            k = s.index("#include")
            s = s[:k] + "#include %s\n" % inc + s[k:]
    mask = code_mask(s)
    notes = []
    for e in spec["entries"]:
        gap = r"\s*" if e["ret"].rstrip().endswith("*") else r"\s+"
        # `static` is optional: a flat module's definitions are its public API and carry none, while
        # a vtable module's are private - only the namespace is exported - so every one of them is
        # static and the anchored pattern found nothing.
        sig = re.compile(
            r"^(?:static\s+)?%s%s%s\s*\((?P<params>[^;{]*)\)\s*\{" % (re.escape(e["ret"]), gap, re.escape(e["flat"])),
            re.M,
        )
        # EVERY definition, not just the first. A capability and its complement are two arms of one
        # entry, both compiled alternately, and converting only the first left the other arm still
        # defining the flat name while the initializer named a static that arm never got. The two
        # statics carry the same name because only one arm is ever compiled.
        done = 0
        while True:
            m = next((x for x in sig.finditer(s) if mask[x.start()]), None)
            if not m:
                break
            # the body runs to its matching brace
            i, depth = m.end(), 1
            while i < len(s) and depth:
                if not mask[i]:
                    i += 1  # inside a comment or a literal: not syntax
                    continue
                c = s[i]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                i += 1
            body = s[m.end() : i - 1]

            # every parameter becomes a read off the args member, so the body is untouched below
            defp = parse_params(m.group("params"))
            reads = []
            for k, q in enumerate(e["params"]):
                local = defp[k] if k < len(defp) else q
                # The local mirrors the parameter, and an array parameter is a pointer: `uint8_t o[2]`
                # declares a local array here, which cannot be initialized from the args member.
                ltype = (local["type"] or q["type"]).strip()
                if local["arr"]:
                    ltype = ltype if ltype.endswith("*") else ltype + " *"
                reads.append(
                    "    %s%s = %s.%s_args.%s;"
                    % (ltype if ltype.endswith("*") else ltype + " ", local["name"], obj, e["entry"], q["name"])
                )
            if e["result"]:
                body = land_returns(body, obj, e["result"])
            # No pre-init of the result member: every `return` the original had is preserved above, so
            # the member is written exactly where the value was produced. A zero written first is a
            # store the original never made, and on an enum result zero is a named outcome.
            head = "static void %s_%s(uint8_t *restrict work)\n{\n    (void)work;\n" % (spec["module"], e["entry"])
            head += "\n".join(reads) + ("\n" if reads else "")
            s = s[: m.start()] + head + body + "}" + s[i:]
            mask = code_mask(s)
            done += 1
        if not done:
            notes.append("%s: definition not found" % e["flat"])
            continue

    # the golden prologue: the config include sits ABOVE the enable gate, so the gate can read it,
    # and the declarations are wrapped once.
    gate = "#if %s" % spec["gate"]
    cfg = '#include "protocore_config.h"'
    gi = s.find(gate)
    if gi != -1:
        ci = s.find(cfg)
        if ci == -1 or ci > gi:
            if ci != -1:
                s = s[:ci] + s[s.index("\n", ci) + 1 :]
                gi = s.find(gate)
            s = s[:gi] + cfg + " // the entry point: the enable gate below, and the widths\n\n" + s[gi:]
        if "PROTOCORE_BEGIN_DECLS" not in s:
            gi = s.find(gate)
            eol = s.index("\n", gi) + 1
            # after the gate and the includes that follow it
            k = eol
            while True:
                m2 = re.match(r"[ \t]*#\s*include[^\n]*\n|\s*\n", s[k:])
                if not m2:
                    break
                k += m2.end()
            s = s[:k] + "PROTOCORE_BEGIN_DECLS\n\n" + s[k:]

    # a module that called its own flat names now stages the args and calls the static entry: the
    # borrow is already in hand, so there is no span to fetch.
    # A module with no flat entries left has no self-calls to rewrite. The guard is not cosmetic:
    # "|".join over an empty set is the EMPTY STRING, so the alternation below becomes `()` - which
    # matches at every `(` in the file with an empty group - and the first lookup raises KeyError on
    # ''. That is what a module already on the Ns shape produces, and it took out 51 of the 79
    # families this was run over. Such a module needs `convert shape`, not `gen`.
    byname = {e["flat"]: e for e in spec["entries"] if e.get("flat")}
    mask = code_mask(s)
    selfcalls = set()
    pat = re.compile(r"(?<![\w.>])(%s)\s*\(" % "|".join(re.escape(k) for k in byname)) if byname else None
    at = 0
    while pat is not None:
        m = pat.search(s, at)
        if not m:
            break
        if not mask[m.start()]:
            at = m.end()
            continue
        e = byname[m.group(1)]
        end = N.close_paren(s, m.end())
        if re.match(r"\s*\{", s[end:]):
            at = m.end()  # a definition, not a call
            continue
        a = N.split_args(s[m.end() : end - 1])
        if len(a) != len(e["params"]):
            notes.append("%s: self-call arity %d != %d" % (e["flat"], len(a), len(e["params"])))
            at = m.end()
            continue
        if not enclosing_has_work(s, m.start()):
            notes.append(
                "%s self-call at line %d is inside a helper with no `work` parameter; give the helper "
                "`uint8_t *restrict work` and read its operands off the Ns args"
                % (e["flat"], s[: m.start()].count(chr(10)) + 1)
            )
            at = m.end()
            continue
        staging = ["%s.%s_args.%s = %s;" % (obj, e["entry"], q["name"], v) for q, v in zip(e["params"], a)]
        staging.append("%s_%s(work);" % (spec["module"], e["entry"]))
        try:
            s = N.rewrite(s, m.start(), end, staging, "%s.%s" % (obj, e["result"] or "ok"), pat, mask)
            selfcalls.add(e["entry"])
            mask = code_mask(s)
            at = 0
        except ValueError as ex:
            notes.append("%s self-call at line %d: %s" % (e["flat"], s[: m.start()].count(chr(10)) + 1, ex))
            at = m.end()

    # `return other_entry(...);` becomes the staging, the call, and then the result member assigned
    # to itself - the value is already where the caller reads it. Drop the self-assignment. A comment
    # the return carried describes the call, so it rides up onto it rather than going with the line;
    # enip's `return protocore_eip_build(...); // no command-specific data` is the shape.
    s = drop_self_assign(s, obj)
    s = drop_flat_protos(s, {e["flat"] for e in spec["entries"]})

    # An entry this file calls before the definition it sits above needs a prototype: without one
    # the call is an implicit declaration with external linkage, and the `static` definition below
    # then conflicts with it.
    if selfcalls:
        first = re.search(r"^static void %s_\w+\(uint8_t \*restrict work\)" % spec["module"], s, re.M)
        if first:
            protos = "// The entries this file calls before reaching their definitions.\n" + "".join(
                "static void %s_%s(uint8_t *restrict work);\n" % (spec["module"], n) for n in sorted(selfcalls)
            )
            s = s[: first.start()] + protos + "\n" + s[first.start() :]

    pending_accessor = None
    if not spec.get("owns_state", True):
        # No file-static context: the module holds nothing, so there is no span to take and no
        # borrow to carve. tls_policy.c is the shape - the note says so where the context would be.
        first = re.search(r"^static void %s_\w+\(uint8_t \*restrict work\)" % spec["module"], s, re.M)
        if first:
            s = (
                s[: first.start()]
                + "// --- the entries -----------------------------------------------------------\n\n"
                + "// No context and no borrow: every operand is the caller's. The borrow an entry takes is\n"
                + "// never read.\n\n"
                + s[first.start() :]
            )
    elif spec.get("span") and spec["span"].split("(")[0] not in s:
        pool = POOLS[spec.get("pool", "secure")]
        accessor = (
            "\n// --- the program's shared state, beside the namespace not on it -------------\n\n"
            "// The one owned instance, private to this TU: the pointer to the bytes this module took for\n"
            "// itself. A caller that hands in its own borrow never reaches it.\n"
            "typedef struct\n{\n"
            "    uint8_t *span; ///< %s persistent bytes, or null while the pool was short\n"
            "} %sOwnCtx;\nstatic %sOwnCtx s_own;\n\n"
            "// Not an entry: an entry takes a borrow and this is where that borrow comes from.\n"
            "uint8_t *%s\n{\n"
            "    if (s_own.span == NULL)\n    {\n"
            "        protocore_span sp = %s(%s);\n"
            "        if (span.ok(sp))\n        {\n            s_own.span = sp.buf;\n%s        }\n    }\n"
            "    return s_own.span; // null while the pool was short, which every entry refuses\n}\n\n"
        ) % (spec["borrow"], obj, obj, spec["span"].replace("()", "(void)"), pool["call"], spec["borrow"],
             seat_defaults(spec, spec["module"].upper())[0])
        pending_accessor = accessor
        if pool["include"] not in s:
            k = s.index("#include")
            s = s[:k] + pool["include"] + " " + pool["why"] + "\n" + s[k:]

    # the point of the shape: the context moves into the borrow, with offsets and an assert
    s = funnel(s, spec["module"], spec["borrow"], notes, seat_defaults(spec, spec["module"].upper())[1])
    # The accessor goes in AFTER the carve, because seating a non-zero default dereferences
    # <PRE>_CTX and the funnel is what defines it. Placed before, the file called a macro that is
    # declared further down and gcc read it as an implicit function.
    if pending_accessor:
        ctx_macro = re.search(r"^#define %s_CTX\(w\)[^\n]*\n" % spec["module"].upper(), s, re.M)
        anchor = re.search(r"^static void %s_\w+\(uint8_t \*restrict work\)" % spec["module"], s, re.M)
        at = ctx_macro.end() if ctx_macro else (above_capability_split(s, anchor.start()) if anchor else None)
        if at is not None:
            s = s[:at] + pending_accessor + s[at:]
    s = drop_void_work(s)
    # The accessor above says every entry refuses a null borrow. The funnel is what puts the
    # dereference in, so the refusal goes in right after it, for the entries that gained one.
    s = guard_borrow(s, spec["module"].upper())

    # Every entry is installed, unconditionally: the namespace is one shape in every build, so a
    # host build reaches the same call surface a target does and the suite covers all of it.
    defn = "%s %s = {\n%s};" % (
        ns,
        obj,
        "".join(".%s = %s_%s,\n" % (e["entry"], spec["module"], e["entry"]) for e in spec["entries"]),
    )
    # A vtable module already HAS an initializer - that table is what it exported - so the old one
    # goes before the new one is appended, or the file defines the object twice.
    if spec.get("from") == "vtable":
        old = re.search(
            r"^(?:const\s+)?%s\s+%s\s*=\s*\{.*?\};[ \t]*\r?\n" % (re.escape(ns), re.escape(obj)), s, re.M | re.S
        )
        if old:
            s = s[: old.start()] + s[old.end() :]
            mask = code_mask(s)

    # Before the gate's closing #endif, so the namespace is defined inside the capability - but only
    # when that #endif really does close the file, which means nothing but whitespace follows it. An
    # inner capability arm near the end of the file is NOT the gate, and putting the initializer
    # before one leaves every entry defined after it bound by nothing: base64's url codecs and
    # http_parser's accessors both went that way. A source with no gate yet takes the end of the
    # file, and the shape pass below wraps it.
    end = gate_endif(s)
    s = s[:end] + defn + "\n\nPROTOCORE_END_DECLS\n\n" + s[end:]
    # the golden file shape is the same pass `shape` runs, so it runs here rather than being a
    # second command a conversion can forget: config alone above the gate, everything else below
    s, how = shape_text(s, p, spec.get("gate"))
    notes.append("shape: " + how)
    emit(p, s)
    return notes


GATE = re.compile(r"^#if\s+\(?PROTOCORE_(?:ENABLE|NEED|TLS|HAS)_\w+[^\n]*$", re.M)
CONFIG_INC = '#include "protocore_config.h" // the entry point: the enable gate below, and the widths\n'


ARM = re.compile(r"^#if\s+!?\w+\s*\n(?:[ \t]*#\s*include[^\n]*\n)*#endif[^\n]*\n", re.M)


def order_includes(tail):
    """Inside the gate, the capability arms come first, then the unconditional includes.

    sha256.c opens `#if PROTOCORE_HAS_HW_SHA / #endif`, then `#if !PROTOCORE_HAS_HW_SHA` with the
    software include, and only then the includes both arms need. An arm buried under the plain
    includes reads as an afterthought rather than as the seam.
    """
    m = re.match(r"((?:[ \t]*(?:#\s*include[^\n]*|#\s*if[^\n]*|#\s*endif[^\n]*|)\n)*)", tail)
    if not m or not m.group(1).strip():
        return tail
    block, rest = m.group(1), tail[m.end() :]
    arms = ARM.findall(block)
    plain = ARM.sub("", block)
    plain = [ln for ln in plain.splitlines() if ln.strip()]
    if not arms:
        return tail
    return "".join(arms) + "\n".join(plain) + ("\n" if plain else "") + rest


def shape_file(p):
    """shape_text against a file on disk."""
    if not os.path.exists(p):
        return "missing"
    s = io.open(p, encoding="utf-8").read()
    out, how = shape_text(s, p)
    if out != s:
        emit(p, out)
    return how


def shape_text(s, p, gate=None):
    """Put a file's includes, and everything else, where the golden puts them.

    Above the gate: the config include, and nothing else. A source that includes its own header up
    there is reaching for the enable flag through the module instead of through the config, and a
    context or a static_assert up there is compiled when the capability is off.

    `gate` names the module's own enable macro. Without it the first `#if PROTOCORE_*` in the file
    is taken, and an inner capability arm that opens before the gate is hoisted in its place:
    modbus.c's `#if PROTOCORE_HAS_NET_STACK` went to the top and swallowed the includes and the
    real gate, so a host build compiled the data model with its header cut out.
    """
    orig = s
    g = re.search(r"^#if\s+\(?%s\b[^\n]*$" % re.escape(gate), s, re.M) if gate else None
    if not g:
        g = GATE.search(s)
    if not g:
        return s, "no enable gate"

    head, gate_line, tail = s[: g.start()], g.group(0), s[g.end() :].lstrip("\n")
    # The file comment is what stays on top: the license banner (a run of // lines) and the doc
    # block after it. A file with a banner and no /** */ matched neither before, so its banner was
    # moved below the gate - the license has to be the first thing in the file.
    m = re.search(r"\*/[ \t]*\n", head)
    if m:
        cut = m.end()
    else:
        b = re.match(r"(?:[ \t]*//[^\n]*\n)+", head)
        cut = b.end() if b else 0
    comment = head[:cut].rstrip("\n")
    moved = head[cut:].strip("\n")
    moved = re.sub(r'^#\s*include\s+"protocore_config\.h"[^\n]*\n?', "", moved, flags=re.M).strip("\n")

    # The include guard is the outermost thing in a header: it wraps the gate, never the reverse.
    # `(?:\n|\Z)` because `moved` was stripped of its trailing newline just above - requiring one
    # made the match fail on a header whose guard is the LAST thing before the gate, and the guard
    # was emitted below the gate instead of above it.
    GUARD_RE = r"^#ifndef (\w+)[ \t]*\n#define \1[ \t]*(?:\n|\Z)"
    guard = ""
    gm = re.search(GUARD_RE, moved, re.M) or re.search(GUARD_RE, tail, re.M)
    if gm:
        guard = gm.group(0).rstrip("\n") + "\n"
        moved = moved.replace(gm.group(0), "", 1).strip("\n")
        tail = tail.replace(gm.group(0), "", 1).lstrip("\n")

    # a header opens DECLS inside the gate, so what it wraps is what the gate compiles
    decls = ""
    if p.endswith(".h") and "PROTOCORE_BEGIN_DECLS" in moved:
        moved = moved.replace("PROTOCORE_BEGIN_DECLS", "").strip("\n")
        decls = "\nPROTOCORE_BEGIN_DECLS\n"

    tail = order_includes(tail)

    out = comment + "\n\n" + (guard + "\n" if guard else "") + CONFIG_INC + "\n" + gate_line + "\n" + decls
    if moved:
        out += "\n" + moved + "\n"
    out += "\n" + tail

    if len(out) < len(orig) * 0.95:
        return orig, "REFUSED: output shrank"
    if out.count("{") != orig.count("{") or out.count("}") != orig.count("}"):
        return orig, "REFUSED: braces changed"
    if out == orig:
        return orig, "already golden"
    return out, "config alone above the gate, %d lines moved below" % len(moved.splitlines())


PROVIDES = re.compile(
    r"^\s*#\s*define\s+(\w+)|\}\s*(\w+)\s*;|^\s*extern\s+\w+\s+(\w+)\s*;|typedef\s+[^;{]*?\b(\w+)\s*;", re.M
)


def gate_endif(s):
    """Where the namespace initializer goes: just before the `#endif` that closes the whole file.

    `$` under re.M matches the end of ANY line, so a pattern asking for "an #endif with nothing but
    whitespace after it" matched the first #endif followed by a blank line - an inner capability arm.
    quic_tls.c's initializer landed inside `#if PROTOCORE_ENABLE_PQC_KEX`, so every entry defined
    below it was bound by nothing and no build without PQC linked. A source with no gate yet takes
    the end of the file, and the shape pass wraps it.
    """
    m = re.search(r"^[ \t]*#[ \t]*endif\b[^\n]*\n\s*\Z", s, re.M)
    return m.start() if m else len(s.rstrip()) + 1


def classify_includes(s):
    """The header's includes, split into the ones that move down to the .c and the ones that stay.

    Every include the header carried moves down: the golden header has only the config. An include
    the header's OWN kept text still needs is the exception - a public struct with a by-value member
    of a type that include defines cannot be declared without it, and moving it leaves the header
    naming a type nothing declared. dtls_conn's DtlsConn embeds a DtlsRecordKeys, a DtlsReplayWindow
    and a Tls13KeySchedule, and dropping all three includes broke every consumer of the header.
    """
    kept = "\n".join(module_types(s) + module_macros(s, ""))
    moved, held = [], []
    for x in re.findall(r'^#\s*include\s+("[^"]+")', s, re.M):
        if "protocore_config.h" in x:
            continue
        inc = os.path.join(R, "src", x.strip('"').replace("/", os.sep))
        names = set()
        if os.path.exists(inc):
            names = {g for t in PROVIDES.findall(io.open(inc, encoding="utf-8").read()) for g in t if g}
        if any(re.search(r"(?<![\w])%s(?![\w])" % re.escape(n), kept) for n in names if n):
            held.append(x)
        else:
            moved.append(x)
    return moved, held


def prune_header(p):
    """Drop an include the header never uses. The golden's only include is protocore_config.h."""
    s = io.open(p, encoding="utf-8").read()
    dropped = []
    for m in re.finditer(r'^#\s*include\s+"([^"]+)"[^\n]*\n', s, re.M):
        rel = m.group(1)
        if "protocore_config.h" in rel:
            continue
        inc = os.path.join(R, "src", rel.replace("/", os.sep))
        if not os.path.exists(inc):
            continue
        names = {g for t in PROVIDES.findall(io.open(inc, encoding="utf-8").read()) for g in t if g}
        body = s.replace(m.group(0), "")
        if any(re.search(r"(?<![\w])%s(?![\w])" % re.escape(n), body) for n in names):
            continue
        s = body
        dropped.append(os.path.basename(rel))
    if dropped:
        emit(p, s)
    return dropped


# A test of the BORROW for null. `work` bare, never `e->work` or `s->work`: those name a struct
# member that happens to be called work, is not the borrow, and can legitimately be null.
NULL_BORROW = re.compile(r"^\s*(?:!\s*work|work\s*==\s*NULL|NULL\s*==\s*work)\s*$")
MEMBER_WORK = re.compile(r"(?:->|\.)\s*work\b")


def split_or(cond):
    """Top-level `||` operands of a condition, literal- and paren-aware, with their separators."""
    mask = code_mask(cond)
    parts, depth, start = [], 0, 0
    i = 0
    while i < len(cond):
        if not mask[i]:
            i += 1
            continue
        c = cond[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif depth == 0 and c == "|" and i + 1 < len(cond) and cond[i + 1] == "|":
            parts.append(cond[start:i])
            start = i + 2
            i += 2
            continue
        i += 1
    parts.append(cond[start:])
    return parts


def drop_null_borrow(src, notes):
    """Remove every `!work` disjunct from an `if` condition. Returns the new text.

    A borrow comes from the arena, so `if (!work)` is dead on every call: the branch cannot be
    taken, and the code it guards cannot run. Removing the DISJUNCT leaves the rest of the guard
    standing - `if (!work || !X.args.out)` becomes `if (!X.args.out)`, because an args member is
    set by the caller and genuinely can be null.

    An `if` whose ONLY condition was the borrow is reported rather than rewritten: deleting it
    means deleting the block it guards, and that is a control-flow change to read rather than to
    apply in a sweep of 33 files.
    """
    mask = code_mask(src)
    out, i, changed = [], 0, 0
    pos = 0
    for m in re.finditer(r"\bif\s*\(", src):
        if not mask[m.start()]:
            continue
        close = N.close_paren(src, m.end())
        cond = src[m.end() : close - 1]
        if "work" not in cond or MEMBER_WORK.search(cond):
            continue
        parts = split_or(cond)
        keep = [p for p in parts if not NULL_BORROW.match(p)]
        if len(keep) == len(parts):
            continue
        line = src.count("\n", 0, m.start()) + 1
        if not keep:
            notes.append("line %d: `if (%s)` guards only the borrow; delete the block by hand" % (line, cond.strip()))
            continue
        out.append(src[pos : m.end()])
        # Re-join on the separator the file already used, so a multi-line condition stays multi-line.
        out.append("||".join(keep).strip())
        pos = close - 1
        changed += 1
    out.append(src[pos:])
    return "".join(out), changed


# The extern-table handle shape. Named HANDLE_* rather than NS_*: NS_ENTRY above already means the
# pimpl shape's `void (*f)(struct XInternal *)`, and reusing the name would shadow it.
HANDLE_STRUCT = re.compile(r"typedef struct\s*\{(?P<body>.*?)\}\s*(?P<ns>\w+Ns)\s*;", re.S)
HANDLE_EXTERN = re.compile(r"^[ \t]*extern[ \t]+(?P<ns>\w+Ns)[ \t]+(?P<obj>\w+)[ \t]*;[ \t]*$", re.M)
HANDLE_FN = re.compile(
    r"^[ \t]*void[ \t]*\([ \t]*\*[ \t]*const[ \t]+(?P<name>\w+)[ \t]*\)[ \t]*"
    r"\([ \t]*uint8_t[ \t]*\*[ \t]*restrict[ \t]+work[ \t]*\)[ \t]*;[ \t]*$",
    re.M,
)


def split_handle(hsrc):
    """The namespace struct's members, split into the DATA a caller writes and the ENTRIES it calls.

    A const table cannot carry a writable member, so the two have to become two objects. Returns
    (ns, obj, data_lines, entry_names), or None when the header is not on the extern-table shape.
    """
    m = HANDLE_EXTERN.search(hsrc)
    if not m:
        return None
    ns, obj = m.group("ns"), m.group("obj")
    st = None
    for s in HANDLE_STRUCT.finditer(hsrc):
        if s.group("ns") == ns:
            st = s
    if not st:
        return None
    data, entries = [], []
    for line in st.group("body").splitlines():
        e = HANDLE_FN.match(line)
        if e:
            entries.append(e.group("name"))
        elif line.strip():
            data.append(line.rstrip())
    if not entries:
        return None
    while data and not data[-1].strip():
        data.pop()
    return ns, obj, data, entries


def handle_members(data_lines):
    """The member NAMES a caller writes, so its call sites can be moved onto the vars object."""
    out = []
    for line in data_lines:
        line = re.sub(r"//.*|/\*.*?\*/", "", line).strip().rstrip(";")
        if not line:
            continue
        m = re.search(r"([A-Za-z_]\w*)[ \t]*(?:\[[^\]]*\])?$", line)
        if m:
            out.append(m.group(1))
    return out


def reshape_handle(hsrc, csrc, mod, ns, obj, data, entries):
    """The extern table becomes a `static const` one, and the operands move to `<Obj>V`.

    An extern table's definition is in another translation unit, so a call through it is INDIRECT and
    the compiler cannot read the pointer. Measured on sha256 against the RFC 6234 vectors: the extern
    form keeps one indirect call and one live symbol PER ENTRY at every optimisation level, -O2 -flto
    included. Initialising the table in the HEADER makes it a compile-time fact - the call resolves to
    a named function and becomes direct - and neither the call nor the symbol reaches the image.
    """
    vars_t, objv = obj + "Vars", obj + "V"
    impl = ["protocore_%s_%s" % (mod, e) for e in entries]

    block = ["typedef struct", "{"] + data + ["} %s;" % vars_t, ""]
    block += ["/** @brief The operands and the outcome. */", "extern %s %s;" % (vars_t, objv), ""]
    block += ["/** @brief The entries. */", "typedef struct", "{"]
    block += ["    void (*const %s)(uint8_t *restrict work);" % e for e in entries]
    block += ["} %s;" % ns, ""]
    block += [
        "// What the table binds, defined once in the .c and taking one parameter each: everything",
        "// else an entry needs is an operand in %s or a region of the borrow at a fixed offset." % objv,
    ]
    block += ["void %s(uint8_t *restrict work);" % f for f in impl]
    block += [
        "",
        "// `static const`, initialised HERE rather than `extern` against a definition in the .c: a",
        "// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so",
        "// `%s.%s(work)` resolves to a named function and becomes a DIRECT call. An extern table" % (obj, entries[0]),
        "// leaves the call indirect and the symbol live at every level, -O2 -flto included.",
        "static const %s %s __attribute__((unused)) = {" % (ns, obj),
    ]
    block += ["    .%s = %s," % (e, f) for e, f in zip(entries, impl)]
    block += ["};"]

    old = None
    for s in HANDLE_STRUCT.finditer(hsrc):
        if s.group("ns") == ns:
            old = s
    hout = hsrc[: old.start()] + "\n".join(block) + hsrc[old.end() :]
    # The declaration goes with the comment that introduces it: removing the line alone leaves
    # `/** @brief The one symbol this module exports. */` standing over whatever follows it.
    hout = re.sub(
        r"(?:^[ \t]*(?:/\*\*(?:(?!\*/).)*?\*/|///[^\n]*)[ \t]*\n)?^[ \t]*extern[ \t]+%s[ \t]+%s[ \t]*;[ \t]*\n"
        % (re.escape(ns), re.escape(obj)),
        "",
        hout,
        count=1,
        flags=re.M | re.S,
    )
    hout = re.sub(r"\n{3,}", "\n\n", hout)

    cout = csrc
    for e, f in zip(entries, impl):
        cout = re.sub(r"\bstatic[ \t]+void[ \t]+%s_%s[ \t]*\(" % (re.escape(mod), re.escape(e)), "void %s(" % f, cout)
        cout = re.sub(r"\b%s_%s\b" % (re.escape(mod), re.escape(e)), f, cout)
    cout = re.sub(
        r"^[ \t]*%s[ \t]+%s[ \t]*=[ \t]*\{.*?\};" % (re.escape(ns), re.escape(obj)),
        "/** @brief The operands and the outcome. */\n%s %s;" % (vars_t, objv),
        cout,
        count=1,
        flags=re.S | re.M,
    )
    return hout, cout, objv, handle_members(data)


def move_operands(text, obj, objv, members):
    """`<Obj>.<data member>` -> `<Obj>V.<data member>`, leaving `<Obj>.<entry>(` alone.

    Told apart by member NAME rather than by what follows, so a data member read without a call and
    an entry taken by address are both handled. Comment and literal bytes are skipped: prose naming
    the old spelling is not a call site.
    """
    if not members:
        return text, 0
    pat = re.compile(r"\b%s\.(%s)\b" % (re.escape(obj), "|".join(re.escape(m) for m in members)))
    mask = code_mask(text)
    out, last, n = [], 0, 0
    for m in pat.finditer(text):
        if not mask[m.start()]:
            continue
        out.append(text[last : m.start()])
        out.append("%s.%s" % (objv, m.group(1)))
        last = m.end()
        n += 1
    out.append(text[last:])
    return "".join(out), n


def insert_align_asserts(src, regions):
    """Add one alignment static_assert per unasserted cast region, after the offsets they read.

    Anchored on the LAST thing the asserts depend on - the borrow's size assert if there is one,
    otherwise the last `_OFF_` define - so the offsets and the types are both already declared. An
    assert placed above its own offset does not compile, and an assert placed above the Ctx typedef
    cannot take its alignment.

    @p regions are shapeaudit's "MACRO->Type@OFFSET" strings.
    """
    mask = code_mask(src)
    anchor = -1
    for m in re.finditer(r"\bstatic_assert\s*\(", src):
        if mask[m.start()] and re.search(r"\w+_BORROW", src[m.start() : src.find(";", m.start()) + 1]):
            anchor = max(anchor, src.find(";", m.start()) + 1)
    if anchor < 0:
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+\w*_OFF_\w+.*$", src, re.M):
            anchor = max(anchor, m.end())
    if anchor < 0:
        return src

    body = []
    for r in regions:
        macro, rest = r.split("->", 1)
        typ, off = rest.split("@", 1)
        if not off:
            continue
        body.append(
            'static_assert(%s %% _Alignof(%s) == 0,\n'
            '              "%s is not a multiple of alignof(%s) - %s() would return a misaligned "\n'
            '              "pointer; pad the region ahead of it");' % (off, typ, off, typ, macro)
        )
    if not body:
        return src
    note = (
        "\n\n// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to\n"
        "// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are\n"
        "// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size\n"
        "// assert above bounds the far end of the chain and says nothing about where a region begins.\n"
    )
    return src[:anchor] + note + "\n".join(body) + src[anchor:]


def main():
    global DRY
    # A header carries the section signs and dashes its RFC citations are written with, and the
    # console's default code page cannot encode them: printing the diff raised UnicodeEncodeError
    # part way through and took the run with it. The bytes are not the point of the diff, so an
    # unencodable one is replaced rather than fatal.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    argv = [a for a in sys.argv if a != "--dry"]
    DRY = len(argv) != len(sys.argv)
    # `align` takes no operand: with no paths it sweeps src/, which is the usual way to run it.
    if len(argv) < 3 and not (len(argv) == 2 and argv[1] in ("align", "unnull", "handle")):
        print(__doc__)
        return 2
    sys.argv = argv
    cmd, arg = argv[1], (argv[2] if len(argv) > 2 else "")
    if DRY:
        print("--- DRY RUN: the diff below is what would be written, and nothing was ---")
    if cmd == "shape":
        # The golden's file shape. sha256.c states one thing above the enable gate - the config
        # include that defines the gate - and everything else below it, so nothing outside the
        # capability is compiled. sha256.h includes only the config, and opens DECLS inside the gate.
        for rel in sys.argv[2:]:
            p = os.path.join(R, rel.replace("/", os.sep))
            how = shape_file(p)
            if p.endswith(".h") and os.path.exists(p):
                gone = prune_header(p)
                if gone:
                    how += "; dropped unused " + ", ".join(gone)
            print("%-40s %s" % (os.path.basename(rel), how))
        return 0
    if cmd == "pimpl":
        # A module that is already a namespace but reaches its state through an Internal handle.
        import pimpl as P

        hp = os.path.join(R, arg.replace("/", os.sep))
        spec = P.scan(hp)
        print("module %s | ns %s | gate %s | borrow %s" % (spec["module"], spec["ns"], spec["gate"], spec["borrow"]))
        print("entries: " + ", ".join(spec["entries"]))
        # Read every file fully before opening any of them for write: `open(p, "w")` truncates as
        # soon as it is evaluated, so a read nested inside the write call reads the file it just
        # emptied.
        sp = os.path.join(R, spec["source"].replace("/", os.sep))
        hsrc = io.open(hp, encoding="utf-8").read()
        ssrc = io.open(sp, encoding="utf-8").read()
        hout = P.convert_header(hsrc, spec)
        sout = P.convert_source(ssrc, spec)
        print("header:", spec["header"])
        emit(hp, hout)
        print("source:", spec["source"])
        emit(sp, sout)
        total, files = P.convert_calls(spec, ["src", "test", "examples", "vendor", "include"], emit)
        print("call sites: %d" % total)
        for rel, n in files:
            print("   %-70s %d" % (rel, n))
        print("NEXT: state %s in protocore_config.h and sum it into the arena" % spec["borrow"])
        return 0
    if cmd == "unnull":
        # A BORROW IS NEVER NULL: it comes from the arena, so `if (!work)` is a dead branch on every
        # call. This removes the DISJUNCT and leaves the rest of the guard standing, because an args
        # member is set by the caller and genuinely can be null.
        rels = [a for a in sys.argv[2:] if not a.startswith("-")]
        if not rels:
            rels = []
            for dirpath, _, names in os.walk(os.path.join(R, "src")):
                for n in names:
                    if n.endswith(".c"):
                        rels.append(os.path.relpath(os.path.join(dirpath, n), R).replace("\\", "/"))
        total, files, notes = 0, 0, []
        for rel in sorted(rels):
            p = os.path.join(R, rel.replace("/", os.sep))
            src = io.open(p, encoding="utf-8").read()
            if "work" not in src:
                continue
            these = []
            out, n = drop_null_borrow(src, these)
            for x in these:
                notes.append("%s %s" % (rel, x))
            if not n:
                continue
            print("unnull: %-70s %d" % (rel, n))
            emit(p, out)
            total += n
            files += 1
        print("%d branch%s in %d file%s" % (total, "es"[: 2 * (total != 1)], files, "s"[: files != 1]))
        for x in notes:
            print("   NOTE " + x)
        return 0

    if cmd == "handle":
        # An extern table's calls are indirect and never fold; a static const table's are direct and
        # leave nothing behind. See reshape_handle for the measurement.
        rels = [a for a in sys.argv[2:] if not a.startswith("-")]
        if not rels:
            import shapeaudit as SA

            rels = [r for r in SA.modules(["src"]) if r.endswith(".h")]
        roots = ("src", "test", "examples", "include")
        tree = []
        for root in roots:
            base = os.path.join(R, root)
            if not os.path.isdir(base):
                continue
            for dp, _, fns in os.walk(base):
                if ".pio" in dp:
                    continue
                tree += [os.path.join(dp, fn) for fn in fns if fn.endswith((".c", ".h"))]

        done, moved = 0, 0
        for rel in rels:
            hp = os.path.join(R, rel.replace("/", os.sep))
            cp = hp[:-2] + ".c"
            if not os.path.exists(cp):
                continue
            hsrc = io.open(hp, encoding="utf-8").read()
            split = split_handle(hsrc)
            if not split:
                continue
            ns, obj, data, entries = split
            mod = os.path.basename(hp)[:-2]
            csrc = io.open(cp, encoding="utf-8").read()
            hout, cout, objv, members = reshape_handle(hsrc, csrc, mod, ns, obj, data, entries)
            cout, own = move_operands(cout, obj, objv, members)
            # A caller writing an operand moves with it, or it writes the const table and will not
            # compile. The header and its call sites HAVE to land together.
            total, files = 0, []
            for p in tree:
                if p in (hp, cp):
                    continue
                s = io.open(p, encoding="utf-8", errors="replace").read()
                if obj + "." not in s:
                    continue
                out, n = move_operands(s, obj, objv, members)
                if n:
                    files.append((p, out))
                    total += n
            print("handle: %-56s %s -> %s  entries=%d operands=%d(+%d own)" % (rel, obj, objv, len(entries), total, own))
            emit(hp, hout)
            emit(cp, cout)
            for p, out in files:
                emit(p, out)
            done += 1
            moved += total + own
        print("%d module%s, %d operand site%s" % (done, "s"[: done != 1], moved, "s"[: moved != 1]))
        return 0

    if cmd == "align":
        # State, per region reached through a cast, that its OFFSET is a multiple of the alignment
        # the cast requires. The arena aligns the base up to PROTOCORE_ARENA_MAX_ALIGN, so the
        # offset is the whole claim - and it is a compile-time constant, so it is a static_assert
        # and never a runtime branch. The size assert already in each file bounds the far end of the
        # chain and says nothing about where any region BEGINS: one odd-sized region inserted
        # earlier leaves every later cast misaligned while the total still fits.
        #
        # An assert that fails is the point, not a setback: it is a misalignment that was already
        # there and had no diagnostic.
        import shapeaudit as SA

        rels = sys.argv[2:] or [r for r in SA.modules(["src"]) if r.endswith(".h")]
        rels = [r for r in rels if not r.startswith("-")]
        touched = 0
        for rel in rels:
            try:
                _, _, c = SA.design_of(rel)
            except Exception as ex:
                print("   SKIPPED %s (%s)" % (rel, ex))
                continue
            if not c or not c["cast_regions_unaligned"]:
                continue
            cp = os.path.join(R, rel.replace("/", os.sep))[:-2] + ".c"
            src = io.open(cp, encoding="utf-8").read()
            out = insert_align_asserts(src, c["cast_regions_unaligned"])
            if out == src:
                print("   no anchor to insert after: %s" % rel)
                continue
            print("align:", rel, "->", ", ".join(c["cast_regions_unaligned"]))
            emit(cp, out)
            touched += 1
        print("%d module%s" % (touched, "s"[: touched != 1]))
        return 0

    if cmd == "funnel":
        # A module converted before the funnel existed has the names and not the point. This moves
        # its file-static context into the borrow without re-running the whole conversion.
        p = os.path.join(R, arg.replace("/", os.sep))
        module = os.path.basename(p)[:-2]
        borrow = sys.argv[3] if len(sys.argv) > 3 else "PROTOCORE_%s_BORROW" % module.upper()
        notes = []
        src = io.open(p, encoding="utf-8").read()
        out = funnel(src, module, borrow, notes)
        if out == src:
            print("unchanged:", arg)
        else:
            print("funnelled:", arg, "->", borrow)
            emit(p, out)
        for x in dict.fromkeys(notes):  # one line per distinct note: a rescan re-reports each skip
            print("   NOTE", x)
        return 0
    if cmd == "scan":
        # A module already on the Ns shape has no flat declarations to read, so it is scanned
        # for what it actually is rather than answered with an empty spec.
        hp = os.path.join(R, arg.replace("/", os.sep))
        text = io.open(hp, encoding="utf-8").read()
        is_ns = re.search(r"^\s*extern\s+(?:const\s+)?\w+Ns\s+\w+\s*;", text, re.M) and not GOLDEN_ENTRY.search(text)
        # A table DEFINED in the header - `static const <X>Ns <obj> = {...}` - is a namespace too,
        # and rawmemcpy is one: fourteen entries, no extern. Without this it fell to the flat scan,
        # which reads its single non-inline prototype and answers with a one-entry spec.
        is_static_ns = re.search(r"^\s*static\s+(?:const\s+)?\w+Ns\s+\w+\b", text, re.M) and VTABLE_ENTRY.search(text)
        if is_static_ns and not is_ns:
            spec = scan_vtable(hp)
        elif not is_ns:
            spec = scan(hp)
        elif ns_entries(text):
            spec = scan_ns(hp)  # the `struct <X>Internal *ctx` shape
        elif VTABLE_ENTRY.search(text):
            spec = scan_vtable(hp)  # a plain function-pointer table
        else:
            spec = scan_ns(hp)
        print(json.dumps(spec, indent=2))
        return 0
    if cmd == "gen":
        spec = json.load(io.open(arg, encoding="utf-8"))
        # A module already on the Ns shape keeps its args and results and changes only what the
        # entries take, so it goes down its own path rather than through the flat rebuild.
        if spec.get("from") == "ns":
            notes = gen_ns(spec)
            for x in notes:
                print("   ", x)
            # A refused module is still on the old shape, so rewriting its call sites would leave
            # every caller passing a borrow to entries that do not take one.
            if any(x.startswith("REFUSED:") for x in notes):
                return 1
            print("call sites:")
            total, skipped = rewrite_ns_calls(spec)
            for rel, line, why in skipped:
                print("   SKIPPED %s:%d  %s" % (rel, line, why))
            print("   total:", total)
            if spec.get("owns_state"):
                print("NEXT: state %s in protocore_config.h and sum it into the arena" % spec["borrow"])
            return 0
        # The object is named after the module, and a module whose own public type carries that
        # name would have `EdgeFetch f;` mean the namespace instead of the type. Caught here: the
        # collision only shows up as a wall of unrelated syntax errors at the call sites.
        own = set()
        for t in spec.get("types", []):
            m = re.search(r"\}\s*(\w+)\s*;\s*$", t) or re.search(r"(\w+)\s*;\s*$", t)
            if m:
                own.add(m.group(1))
        if spec["object"] in own:
            print(
                "REFUSED: %s names both this module's public type and the namespace object.\n"
                '  Set "object" in the spec to a name the module does not already define.' % spec["object"]
            )
            return 1
        # An entry is defined as `static void <module>_<entry>(uint8_t *restrict work)`. A private
        # helper already carrying that name becomes a second definition at a different signature:
        # json.c had `static void json_put_raw(protocore_json_writer *, const char *)` and the
        # conversion wrote `static void json_put_raw(uint8_t *restrict)` beside it.
        csrc = io.open(os.path.join(R, spec["source"].replace("/", os.sep)), encoding="utf-8").read()
        taken = []
        for e in spec["entries"]:
            name = "%s_%s" % (spec["module"], e["entry"])
            if name in (x["flat"] for x in spec["entries"]) or name == e.get("impl"):
                continue  # the module's own function, which this pass is renaming
            if re.search(r"^\s*static\s[^;{]*\b%s\s*\([^)]*\)\s*\n?\s*\{" % re.escape(name), csrc, re.M):
                taken.append(name)
        if taken:
            print(
                "REFUSED: %s already defines %s as a private helper, and that is the name the\n"
                "  entry would take. Rename the helper in the .c first, then re-run."
                % (spec["source"], ", ".join(taken))
            )
            return 1
        # Every entry takes `uint8_t *restrict work`, so a parameter already called that would be
        # redeclared inside the entry that reads it off the args member.
        clash = sorted({p["name"] for e in spec["entries"] for p in e["params"] if p["name"] == "work"})
        if clash:
            print(
                "REFUSED: this module has a parameter named `work`, which is the borrow every entry\n"
                "  takes. Rename that parameter in the header and the .c first, then re-run."
            )
            return 1
        # What a call site passes as the borrow. A module that owns state hands out its own span;
        # one that owns none never reads the pointer, so its callers keep a nominal buffer.
        if spec.get("owns_state", True):
            spec.setdefault("span", "protocore_%s_span()" % spec["module"])
        else:
            spec.setdefault("span", "%s_work" % spec["module"])
        hp = os.path.join(R, spec["header"].replace("/", os.sep))
        original = io.open(hp, encoding="utf-8").read()
        require_gate(spec)
        # An entry defined on only ONE arm is reported, never gated. Host builds every capability
        # and runs every test, so a member the host cannot see is a test that cannot run: the
        # answer is a real host arm for the missing one, not a narrower namespace.
        for name, arm in entry_arms(spec).items():
            print("   NOTE %s is defined only under `%s`; give the other arm a real definition" % (name, arm))
        print("header:", spec["header"])
        regenerated = gen_header(spec, original)
        for name in dropped_names(spec, original, regenerated):
            print("   NOTE dropped from the header: %s" % name)
        for tag in dropped_tag_decls(original, regenerated):
            print("   NOTE the forward declaration of `%s` went, and the new header still names it" % tag)
        for name in gutted_macros(original, regenerated):
            print("   NOTE `%s` kept its name and lost its body - it now expands to nothing" % name)
        emit(hp, regenerated)
        print("source:", spec["source"])
        notes = restructure_source(spec)
        for x in dict.fromkeys(notes):  # one line per distinct note: a rescan re-reports each skip
            print("   NOTE", x)
        print("call sites:")
        total, skipped = rewrite_calls(spec)
        print("   total:", total)
        # A successful rewrite restarts the scan, so a site refused earlier is met again on every
        # pass. Report each one once.
        for rel, ln, why in sorted(set(skipped)):
            print("   SKIPPED %s:%d  %s" % (rel, ln, why))
        if spec.get("owns_state", True):
            print("NEXT: state %s in protocore_config.h and sum it into the arena, then" % spec["borrow"])
            print(
                "      add the pool the span comes from to every env that builds this module:\n"
                "      harness.py env update <env> --src mmgr/%s.c mmgr/span.c mmgr/arena.c"
                % spec.get("pool", "secure")
            )
        else:
            print("NEXT: nothing - this module holds no state, so it carves no borrow")
        for suite in spec.get("suites", []) if not DRY else []:
            r = subprocess.run(
                [sys.executable, os.path.join(R, "test", "harness.py"), "runners", "gen", suite],
                cwd=R,
                capture_output=True,
                text=True,
            )
            print("   runner:", (r.stdout or r.stderr).strip())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
