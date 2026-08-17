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


def module_types(header_text):
    """The type vocabulary the module's own header defined: callbacks, enums, public structs.

    Regenerating the header must not delete these - they are the module's language, not its call
    surface. A storage struct that belongs in the .c is moved by naming it in the spec.

    The body is brace-matched rather than taken up to the first `} name;`: a struct with a nested
    anonymous struct closes that one first, and stopping there captured `} lru;` as though it were
    a whole type and left the rest of the struct behind.
    """
    out, mask = [], code_mask(header_text)
    for m in re.finditer(r"(?:^/\*\*(?:(?!\*/).)*?\*/\s*)?^typedef\b", header_text, re.M | re.S):
        if not mask[m.end() - 1]:
            continue
        semi = header_text.find(";", m.end())
        brace = header_text.find("{", m.end())
        end = typedef_end(header_text, brace) if brace != -1 and (semi == -1 or brace < semi) else semi + 1
        t = header_text[m.start() : end].strip()
        if re.search(r"\}\s*\w+(Args|Ns|Bind)\s*;\s*$", t):
            continue  # generated shapes, not the module's own
        out.append(t)
    return out


# just the directive and its line continuations. A doc block above it is left where it is: pulling
# it in with re.DOTALL swallowed everything between blocks.
MACRO = re.compile(r"^#[ \t]*define[ \t]+(\w+)[^\n]*(?:\\\n[^\n]*)*", re.M)


def module_macros(header_text, guard):
    """The constants the module's header published: lengths, caps, wire values.

    Regenerating the header must not delete these either - a consumer sizes its buffers with them.
    The include guard and anything the config owns are skipped.
    """
    out = []
    for m in MACRO.finditer(header_text):
        name = m.group(1)
        if name == guard or name.endswith("_BORROW"):
            continue
        out.append(m.group(0).strip())
    return out


def camel(mod):
    return "".join(p.capitalize() for p in re.split(r"[_\-]", mod) if p)


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


def scan(hpath):
    s = io.open(hpath, encoding="utf-8").read()
    mod = os.path.splitext(os.path.basename(hpath))[0]
    gate = re.search(r"^#if\s+(PROTOCORE_ENABLE_\w+)", s, re.M)
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
    # Every include the header carried moves down to the .c: the golden header has only the config.
    # An include the header's OWN kept text still needs is the exception - a public struct with a
    # by-value member of a type that include defines cannot be declared without it, and moving it
    # leaves the header naming a type nothing declared.
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
    suites = []
    for dp, dns, _ in os.walk(os.path.join(R, "test")):
        for d in dns:
            if d == "test_" + mod:
                suites.append(os.path.relpath(os.path.join(dp, d), R).replace("\\", "/"))
    # A module with no file-static context holds nothing between calls, so it carves no borrow,
    # states none, and needs no span accessor - tls_policy is the shape. That is read off the .c
    # rather than chosen: the state is there or it is not.
    cpath = hpath[:-1] + "c"
    csrc = io.open(cpath, encoding="utf-8").read() if os.path.exists(cpath) else ""
    return {
        "macros": module_macros(s, re.search(r"#ifndef (\w+)", s).group(1) if re.search(r"#ifndef (\w+)", s) else ""),
        "types": module_types(s),
        "suites": suites,
        "moved_includes": moved,
        "module": mod,
        "ns": camel(mod) + "Ns",
        "object": camel(mod),
        "gate": gate.group(1) if gate else "",
        "header": os.path.relpath(hpath, R).replace("\\", "/"),
        "source": os.path.relpath(hpath, R).replace("\\", "/")[:-1] + "c",
        "borrow": "PROTOCORE_%s_BORROW" % mod.upper(),
        "held_includes": held,
        "owns_state": bool(find_context(csrc)),
        "brief": first_sentence(doc_tags(doc_above(s, s.find("#ifndef")))[0]),
        "entries": entries,
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
        out.append(" * @var %s::%s  %s" % (ns, r, lead_lower(one_line(doc, 68)) or RESULT_DOC.get(r, "what a call reports")))
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
    for t in foreign_types(spec):
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
            if base in fw:
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


def rewrite_calls(spec, roots=("src", "test")):
    """Every call to a flat name becomes staging + entry + the result member."""
    obj = spec["object"]
    byname = {e["flat"]: e for e in spec["entries"]}
    pat = re.compile(r"(?<![\w.>])(%s)\s*\(" % "|".join(re.escape(k) for k in byname))
    total, skipped = 0, []
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
                at, n = 0, 0
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
                        skipped.append((rel, s[: m.start()].count("\n") + 1, "definition, not a call - convert it by hand"))
                        at = m.end()
                        continue
                    a = N.split_args(s[m.end() : end - 1])
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
                    except ValueError as ex:
                        skipped.append((rel, s[: m.start()].count("\n") + 1, str(ex)))
                        at = m.end()
                if n:
                    for inc in spec.get("moved_includes", []):
                        if inc not in s:
                            k = s.index("#include")
                            s = s[:k] + "#include %s\n" % inc + s[k:]
                    s = declare_work(s, spec)
                    print("   %-72s %d" % (rel, n))
                    emit(p, s)
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
    # After the last include that is NOT inside a conditional arm. An include under `#if CAP` is
    # compiled only when that capability is on, and a declaration parked beside it disappears with
    # it - leaving every call site in the file naming an identifier that is not there.
    last, depth = None, 0
    for m in re.finditer(r"^[ \t]*#\s*(if\w*|else|elif|endif|include)\b[^\n]*\n", s, re.M):
        kind = m.group(1)
        if kind.startswith("if"):
            depth += 1
        elif kind == "endif":
            depth = max(0, depth - 1)
        elif kind == "include" and depth == 0:
            last = m
    k = last.end() if last else 0
    while s[k : k + 1] == "\n":
        k += 1
    return s[:k] + decl + s[k:]


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
        if j < len(body) and value:
            spans.append((mm.start(), j + 1, value))
    for a, b, value in reversed(spans):
        indent = re.match(r"[ \t]*", body[body.rfind("\n", 0, a) + 1 :]).group(0)
        body = body[:a] + "%s.%s = %s;\n%sreturn;" % (obj, result, value, indent) + body[b:]
    mt = re.search(r"\n[ \t]*return;[ \t]*(//[^\n]*?)[ \t]*\n?\s*$", body)
    if mt:
        body = body[: mt.start()].rstrip() + " " + mt.group(1) + "\n"
    return re.sub(r"\n[ \t]*return;\s*$", "\n", body)


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
        sig = re.compile(
            r"^%s%s%s\s*\((?P<params>[^;{]*)\)\s*\{" % (re.escape(e["ret"]), gap, re.escape(e["flat"])),
            re.M,
        )
        m = sig.search(s)
        if not m or not mask[m.start()]:
            notes.append("%s: definition not found" % e["flat"])
            continue
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
    byname = {e["flat"]: e for e in spec["entries"]}
    pat = re.compile(r"(?<![\w.>])(%s)\s*\(" % "|".join(re.escape(k) for k in byname))
    mask = code_mask(s)
    selfcalls = set()
    at = 0
    while True:
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
    # to itself - the value is already where the caller reads it. Drop the self-assignment.
    s = re.sub(r"(?m)^[ \t]*%s\.(\w+) = %s\.\1;[ \t]*\r?\n" % (re.escape(obj), re.escape(obj)), "", s)

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
            "        protocore_span sp = protocore_secure_persist_span(%s);\n"
            "        if (span.ok(sp))\n        {\n            s_own.span = sp.buf;\n        }\n    }\n"
            "    return s_own.span; // null while the pool was short, which every entry refuses\n}\n\n"
        ) % (spec["borrow"], obj, obj, spec["span"].replace("()", "(void)"), spec["borrow"])
        first = re.search(r"^static void %s_\w+\(uint8_t \*restrict work\)" % spec["module"], s, re.M)
        if first:
            s = s[: first.start()] + accessor + s[first.start() :]
        if '#include "mmgr/secure.h"' not in s:
            k = s.index("#include")
            s = s[:k] + '#include "mmgr/secure.h" // the persistent end this module\'s state is taken from\n' + s[k:]

    # the point of the shape: the context moves into the borrow, with offsets and an assert
    s = funnel(s, spec["module"], spec["borrow"], notes)
    s = drop_void_work(s)

    defn = "%s %s = {%s};" % (
        ns,
        obj,
        ", ".join(".%s = %s_%s" % (e["entry"], spec["module"], e["entry"]) for e in spec["entries"]),
    )
    end = s.rindex("#endif")
    s = s[:end] + defn + "\n\nPROTOCORE_END_DECLS\n\n" + s[end:]
    # the golden file shape is the same pass `shape` runs, so it runs here rather than being a
    # second command a conversion can forget: config alone above the gate, everything else below
    s, how = shape_text(s, p)
    notes.append("shape: " + how)
    emit(p, s)
    return notes


GATE = re.compile(r"^#if\s+\(?PROTOCORE_(?:ENABLE|TLS|HAS)_\w+[^\n]*$", re.M)
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
    block, rest = m.group(1), tail[m.end():]
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


def shape_text(s, p):
    """Put a file's includes, and everything else, where the golden puts them.

    Above the gate: the config include, and nothing else. A source that includes its own header up
    there is reaching for the enable flag through the module instead of through the config, and a
    context or a static_assert up there is compiled when the capability is off.
    """
    orig = s
    g = GATE.search(s)
    if not g:
        return s, "no enable gate"

    head, gate_line, tail = s[: g.start()], g.group(0), s[g.end():].lstrip("\n")
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

    # the include guard is the outermost thing in a header: it wraps the gate, never the reverse
    guard = ""
    gm = re.search(r"^#ifndef (\w+)[ \t]*\n#define \1[ \t]*\n", moved, re.M) or re.search(r"^#ifndef (\w+)[ \t]*\n#define \1[ \t]*\n", tail, re.M)
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


def main():
    global DRY
    argv = [a for a in sys.argv if a != "--dry"]
    DRY = len(argv) != len(sys.argv)
    if len(argv) < 3:
        print(__doc__)
        return 2
    sys.argv = argv
    cmd, arg = argv[1], argv[2]
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
        total, files = P.convert_calls(spec, ["src", "test", "examples"])
        print("call sites: %d" % total)
        for rel, n in files:
            print("   %-70s %d" % (rel, n))
        print("NEXT: state %s in protocore_config.h and sum it into the arena" % spec["borrow"])
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
        for x in notes:
            print("   NOTE", x)
        return 0
    if cmd == "scan":
        print(json.dumps(scan(os.path.join(R, arg.replace("/", os.sep))), indent=2))
        return 0
    if cmd == "gen":
        spec = json.load(io.open(arg, encoding="utf-8"))
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
                "  Set \"object\" in the spec to a name the module does not already define." % spec["object"]
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
        print("header:", spec["header"])
        emit(hp, gen_header(spec, original))
        print("source:", spec["source"])
        notes = restructure_source(spec)
        for x in notes:
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
                "      harness.py env update <env> --src mmgr/secure.c mmgr/span.c mmgr/arena.c"
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
