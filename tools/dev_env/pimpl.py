"""Take a module that is already a namespace, but reaches its state through an Internal handle, to
the golden.

The tree has two shapes. The golden (sha256) hands every entry one pointer and computes every
region as that pointer plus a compile-time offset. The older pimpl hands every entry a
`struct <X>Internal *ctx` that holds a pointer to the module's storage and a pointer back to the
namespace, so the state is a file-static the entry reaches through two indirections and nothing
proves any span covers it.

The transformation, per module:

  struct XStorage { A a; B b; };          ->   typedef struct { A a; B b; C c; } XCtx;
  struct XInternal { XStorage *store;          #define X_OFF_CTX 0u
                     XNs *ns; C c; };          static_assert(X_OFF_CTX + sizeof(XCtx) <= X_BORROW, ...)
  static XStorage s_store;                     #define X_CTX(w) ((XCtx *)(void *)((w) + X_OFF_CTX))
  static XInternal s_x = {...};

  void (*name)(struct XInternal *ctx);    ->   void (*const name)(uint8_t *restrict work);
  ctx->store->a                           ->   X_CTX(work)->a
  ctx->c                                  ->   X_CTX(work)->c
  ctx->ns->member                         ->   X.member
  X.name(X.internal)                      ->   X.name(protocore_x_span())

The Internal handle's own members are folded into the context: `store` and `ns` are the two
indirections being removed, and anything else it carried was per-module state living in the wrong
record.
"""

import io
import os
import re

from codemask import code_mask

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _block(src, brace):
    """The half-open range inside the braces that start at index `brace`."""
    mask = code_mask(src)
    i, depth = brace + 1, 1
    while i < len(src) and depth:
        if mask[i]:
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
        i += 1
    return brace + 1, i - 1


def _struct_fields(src, name):
    """The text between the braces of `struct <name> { ... };`, or None."""
    m = re.search(r"struct\s+%s\s*\{" % re.escape(name), src)
    if not m:
        return None, None, None
    a, b = _block(src, m.end() - 1)
    end = src.find(";", b)
    return src[a:b], m.start(), end + 1


def scan(hpath):
    """Read the namespace's shape out of the header."""
    h = io.open(hpath, encoding="utf-8").read()
    m = re.search(r"^extern\s+(?P<ns>\w+Ns)\s+(?P<obj>\w+)\s*;", h, re.M)
    if not m:
        raise SystemExit("no `extern <X>Ns <X>;` in " + hpath)
    ns, obj = m.group("ns"), m.group("obj")
    internal = None
    mi = re.search(r"\(\*(?:const\s+)?\w+\)\s*\(\s*struct\s+(\w+Internal)\s*\*", h)
    if mi:
        internal = mi.group(1)
    if internal is None:
        raise SystemExit("entries do not take a struct <X>Internal * in " + hpath)

    entries = re.findall(r"\(\*(?:const\s+)?(\w+)\)\s*\(\s*struct\s+%s\s*\*[^)]*\)" % internal, h)

    # The gate is the enable still open where the namespace is declared, so the conditionals are
    # walked with their nesting rather than taking the last one seen.
    gate, open_gates = None, []
    for d in re.finditer(r"^#\s*(ifndef|ifdef|if|endif)\b(?:\s+(\w+))?[^\n]*$", h[: m.start()], re.M):
        kind, name = d.group(1), d.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            open_gates.append(name if kind == "if" else None)
        elif kind == "endif" and open_gates:
            open_gates.pop()
    for name in open_gates:
        if name and name.startswith("PROTOCORE_ENABLE_"):
            gate = name
    rel = os.path.relpath(hpath, R).replace(os.sep, "/")
    mod = re.sub(r"(?<!^)(?=[A-Z])", "_", obj).lower()
    return {
        "module": mod,
        "ns": ns,
        "object": obj,
        "internal": internal,
        "storage": internal.replace("Internal", "Storage"),
        "gate": gate,
        "borrow": "PROTOCORE_%s_BORROW" % mod.upper(),
        "header": rel,
        "source": rel[:-2] + ".c",
        "entries": entries,
    }


def convert_header(h, spec):
    ns, internal = spec["ns"], spec["internal"]
    # every entry takes the borrow
    h = re.sub(
        r"void\s*\(\*(?:const\s+)?(\w+)\)\s*\(\s*struct\s+%s\s*\*[^)]*\)" % internal,
        lambda m: "void (*const %s)(uint8_t *restrict work)" % m.group(1),
        h,
    )
    # the handle member and its forward declaration go with it
    h = re.sub(r"[ \t]*struct\s+%s\s*\*\s*internal\s*;[^\n]*\n" % internal, "", h)
    h = re.sub(r"[^\n]*@var\s+%s::internal[^\n]*\n" % ns, "", h)
    # The doc block that goes with it is the one DIRECTLY above: `(?!\*/)` stops the optional
    # prefix crossing a `*/`, so it cannot start at the file's own @file block and swallow the
    # include guard, the gate and every args struct between there and this declaration.
    h = re.sub(
        r"(?:/\*\*(?:(?!\*/)[\s\S])*?\*/[ \t]*\n)?[ \t]*struct\s+%s\s*;[^\n]*\n" % internal, "", h, count=1
    )

    # the borrow comes from beside the namespace, as it does for rng
    span = (
        "\n/** @brief Not an entry: an entry takes a borrow and this is where that borrow comes from. */\nuint8_t *protocore_%s_span(void);\n"
        % spec["module"]
    )
    m = re.search(r"^extern\s+%s\s+%s\s*;\s*$" % (ns, spec["object"]), h, re.M)
    h = h[: m.end()] + "\n" + span + h[m.end() :]
    return h


def convert_source(s, spec):
    obj, internal, storage = spec["object"], spec["internal"], spec["storage"]
    PRE = spec["module"].upper()
    ctype = obj + "Ctx"

    sfields, sa, sb = _struct_fields(s, storage)
    ifields, ia, ib = _struct_fields(s, internal)
    if sfields is None or ifields is None:
        raise SystemExit("could not read struct %s / struct %s" % (storage, internal))

    # This pass folds a file-static instance into the borrow. A module that already carves its
    # storage FROM a pool has no instance to fold: auth.c's bind_auth() takes
    # protocore_secure_persist_span(sizeof(struct AuthStorage)) and the handle only caches the
    # pointer. Collapsing that emitted a context with the fields merged in AND a bind_auth still
    # returning `struct AuthStorage *` from a `->store` member that no longer existed - it did not
    # compile, and the diff read as if it had worked.
    if not re.search(r"^static\s+(?:PROTOCORE_\w+\s+)?struct\s+%s\s+\w+\s*[;=]" % re.escape(storage), s, re.M):
        raise SystemExit(
            "REFUSED: struct %s has no file-static instance to fold into the borrow.\n"
            "  This module already takes its storage from a pool, so there is nothing here to move.\n"
            "  What is left is the handle: entries take `uint8_t *restrict work` and read the state\n"
            "  through it directly, with no cached pointer. Convert that by hand." % storage
        )

    # The handle carried two indirections and, sometimes, real state. The indirections go; the
    # state joins the record it should always have been in.
    carried = []
    for line in ifields.splitlines():
        t = line.strip()
        if not t or t.startswith("//") or t.startswith("*") or t.startswith("/*"):
            continue
        if re.match(r"(struct\s+)?%s\s*\*" % re.escape(storage), t):
            continue
        if re.match(r"%s\s*\*" % re.escape(spec["ns"]), t):
            continue
        carried.append(line)

    ctx = "typedef struct\n{\n" + sfields.rstrip("\n") + "\n"
    if carried:
        ctx += "\n" + "\n".join(carried).rstrip("\n") + "\n"
    ctx += "} %s;\n\n" % ctype
    ctx += (
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

    lo, hi = min(sa, ia), max(sb, ib)
    s = s[:lo] + ctx + s[hi:]

    # the two indirections, and the state the handle carried
    s = re.sub(r"ctx\s*->\s*store\s*->\s*", "%s_CTX(work)->" % PRE, s)
    s = re.sub(r"ctx\s*->\s*ns\s*->\s*", "%s." % obj, s)
    s = re.sub(r"(?<![\w.>])ctx\s*->\s*", "%s_CTX(work)->" % PRE, s)

    # every entry takes the borrow
    s = re.sub(r"\(\s*struct\s+%s\s*\*(?:\s*restrict)?\s*ctx\s*\)" % internal, "(uint8_t *restrict work)", s)
    s = re.sub(r"\(void\)\s*ctx\s*;", "(void)work;", s)

    # the file-statics the handle pointed at
    s = re.sub(r"^static\s+struct\s+%s\s+\w+\s*;\s*\n" % storage, "", s, flags=re.M)
    s = re.sub(r"^static\s+struct\s+%s\s+\w+\s*=\s*\{[^;]*;\s*\n" % internal, "", s, flags=re.M)

    # the namespace no longer carries a handle, and its entries are const
    s = re.sub(r",?\s*\.internal\s*=\s*&\w+\s*", "", s)

    span = (
        "\n// Not an entry: an entry takes a borrow and this is where that borrow comes from.\n"
        "uint8_t *protocore_%s_span(void)\n"
        "{\n"
        "    static uint8_t *s_span;\n"
        "    if (s_span == NULL)\n"
        "    {\n"
        "        protocore_span sp = protocore_secure_persist_span(%s);\n"
        "        if (span.ok(sp))\n"
        "        {\n"
        "            s_span = sp.buf;\n"
        "        }\n"
        "    }\n"
        "    return s_span; // null while the pool was short, which every entry refuses\n"
        "}\n"
    ) % (spec["module"], spec["borrow"])
    m = re.search(r"^%s\s+%s\s*=\s*\{" % (spec["ns"], obj), s, re.M)
    if m:
        s = s[: m.start()] + span + "\n" + s[m.start() :]
    return s


def convert_calls(spec, roots, emit=None):
    """`X.entry(X.internal)` -> `X.entry(protocore_x_span())`, tree-wide.

    @p emit is the caller's write primitive. Writing with io.open here bypassed it, so `--dry`
    printed the header and source diffs it would apply and then rewrote every CALL SITE for real:
    a refused conversion left protocore.c and http.c calling an accessor no header declared.
    """
    obj = spec["object"]
    if emit is None:

        def emit(p, text):
            io.open(p, "w", encoding="utf-8", newline="").write(text)

    pat = re.compile(r"(?<![\w.>])%s\s*\.\s*internal(?![\w])" % re.escape(obj))
    repl = "protocore_%s_span()" % spec["module"]
    total, files = 0, []
    for root in roots:
        for dirpath, _dirs, names in os.walk(os.path.join(R, root)):
            for n in names:
                if not n.endswith((".c", ".h")):
                    continue
                p = os.path.join(dirpath, n)
                src = io.open(p, encoding="utf-8").read()
                if obj + "." not in src:
                    continue
                mask = code_mask(src)
                out, last, cnt = [], 0, 0
                for m in pat.finditer(src):
                    if not mask[m.start()]:
                        continue
                    out.append(src[last : m.start()])
                    out.append(repl)
                    last = m.end()
                    cnt += 1
                if cnt:
                    out.append(src[last:])
                    emit(p, "".join(out))
                    total += cnt
                    files.append((os.path.relpath(p, R), cnt))
    return total, files
