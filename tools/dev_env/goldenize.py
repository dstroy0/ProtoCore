"""One module converter: flat C module -> the sha256 golden shape.

Two phases so the inference is reviewable before it writes anything:

  goldenize.py scan <module.h>          print the spec it infers, as JSON
  goldenize.py gen  <spec.json>         write the header, restructure the .c, rewrite call sites

The spec is small and hand-editable, so a module the scanner reads wrong is corrected by editing
one JSON file rather than by writing another script. Call-site rewriting goes through nsconv, which
is literal-aware, hoists to the statement (not the line), and refuses loop conditions.
"""

import io, json, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsconv as N
from codemask import code_mask
from funnel import funnel

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

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


def module_types(header_text):
    """The type vocabulary the module's own header defined: callbacks, enums, public structs.

    Regenerating the header must not delete these - they are the module's language, not its call
    surface. A storage struct that belongs in the .c is moved by naming it in the spec.
    """
    out = []
    for m in TYPEDEF.finditer(header_text):
        t = m.group(0).strip()
        if re.search(r"\}\s*\w+(Args|Ns|Bind)\s*;", t):
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
        rtype = {"n": "int", "ms": "uint32_t", "text": "const char *", "value": "uint16_t"}.get(result, "int")
        if ret not in RESULT and ret.endswith("*"):
            result, rtype = "ptr", ret
        entries.append(
            {
                "result_type": rtype,
                "flat": name,
                "entry": re.sub(r"^(protocore_)?%s_?" % mod, "", name) or name,
                "ret": ret,
                "result": result,
                "params": parse_params(m.group("params")),
            }
        )
    # every include the header carried moves down to the .c: the golden header has only the config
    moved = [x for x in re.findall(r'^#\s*include\s+("[^"]+")', s, re.M) if "protocore_config.h" not in x]
    suites = []
    for dp, dns, _ in os.walk(os.path.join(R, "test")):
        for d in dns:
            if d == "test_" + mod:
                suites.append(os.path.relpath(os.path.join(dp, d), R).replace("\\", "/"))
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
        "entries": entries,
    }


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
        lines.append("/** @brief What %s takes. */" % e["entry"])
        lines.append("typedef struct")
        lines.append("{")
        fw = foreign_types(spec)
        for p in e["params"]:
            t = p["type"]
            base = t.replace("const", " ").replace("*", " ").strip()
            if base in fw:
                t = t.replace(base, "struct " + base)
            lines.append("    %s %s%s;" % (re.sub(r"\s+", " ", t).strip(), p["name"], p["arr"]))
        lines.append("} %s;" % an)
        lines.append("")

    results = []
    for e in spec["entries"]:
        if e["result"] and e["result"] not in results:
            results.append(e["result"])

    body = ["typedef struct", "{"]
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
    if spec.get("span"):
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

    i = original.find("#ifndef")
    head = original[:i] if i != -1 else original.split("*/")[0] + "*/\n\n"
    g = re.search(r"#ifndef (\w+)\n#define \1", original)
    guard = g.group(1) if g else "PROTOCORE_%s_H" % spec["module"].upper()
    return (
        head
        + "#ifndef %s\n#define %s\n\n" % (guard, guard)
        + '#include "protocore_config.h" // the entry point: protocore_types.h for the widths\n\n'
        + "#if %s\n\n" % spec["gate"]
        + "PROTOCORE_BEGIN_DECLS\n\n"
        + "// %s - the bytes this module runs out of - is stated in protocore_config.h, which sums it\n"
        % spec["borrow"]
        + "// into its arena. A caller takes them once and passes the pointer to every call. How they are\n"
        + "// carved is this module's and is never named here.\n\n"
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
                    io.open(p, "w", encoding="utf-8", newline="").write(s)
                    print("   %-72s %d" % (rel, n))
                    total += n
    return total, skipped


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
            reads.append(
                "    %s %s%s = %s.%s_args.%s;"
                % (local["type"] or q["type"], local["name"], local["arr"], obj, e["entry"], q["name"])
            )
        # a value return lands on the result member
        if e["result"]:
            body = re.sub(
                r"\breturn\s+([^;]+);",
                lambda mm: "%s.%s = %s;\n        return;" % (obj, e["result"], mm.group(1).strip()),
                body,
            )
        head = "static void %s_%s(uint8_t *restrict work)\n{\n    (void)work;\n" % (spec["module"], e["entry"])
        if e["result"]:
            head += "    %s.%s = 0;\n" % (obj, e["result"]) if e["result"] != "ok" else ""
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
            mask = code_mask(s)
            at = 0
        except ValueError as ex:
            notes.append("%s self-call at line %d: %s" % (e["flat"], s[: m.start()].count(chr(10)) + 1, ex))
            at = m.end()

    if spec.get("span") and spec["span"].split("(")[0] not in s:
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

    defn = "%s %s = {%s};" % (
        ns,
        obj,
        ", ".join(".%s = %s_%s" % (e["entry"], spec["module"], e["entry"]) for e in spec["entries"]),
    )
    end = s.rindex("#endif")
    s = s[:end] + defn + "\n\nPROTOCORE_END_DECLS\n\n" + s[end:]
    io.open(p, "w", encoding="utf-8", newline="").write(s)
    return notes


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, arg = sys.argv[1], sys.argv[2]
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
        io.open(hp, "w", encoding="utf-8", newline="").write(hout)
        print("header written:", spec["header"])
        io.open(sp, "w", encoding="utf-8", newline="").write(sout)
        print("source written:", spec["source"])
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
            io.open(p, "w", encoding="utf-8", newline="").write(out)
            print("funnelled:", arg, "->", borrow)
        for x in notes:
            print("   NOTE", x)
        return 0
    if cmd == "scan":
        print(json.dumps(scan(os.path.join(R, arg.replace("/", os.sep))), indent=2))
        return 0
    if cmd == "gen":
        spec = json.load(io.open(arg, encoding="utf-8"))
        spec.setdefault("span", "protocore_%s_span()" % spec["module"])
        hp = os.path.join(R, spec["header"].replace("/", os.sep))
        original = io.open(hp, encoding="utf-8").read()
        io.open(hp, "w", encoding="utf-8", newline="").write(gen_header(spec, original))
        print("header written:", spec["header"])
        notes = restructure_source(spec)
        print("source restructured:", spec["source"])
        for x in notes:
            print("   NOTE", x)
        print("call sites:")
        total, skipped = rewrite_calls(spec)
        print("   total:", total)
        for rel, ln, why in skipped:
            print("   SKIPPED %s:%d  %s" % (rel, ln, why))
        print("NEXT: state %s in protocore_config.h and sum it into the arena" % spec["borrow"])
        for suite in spec.get("suites", []):
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
