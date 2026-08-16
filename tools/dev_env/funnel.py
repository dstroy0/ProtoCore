"""Move a module's file-static context into the caller's borrow.

This is the point of the shape, not a detail of it: one pointer arrives, every region is that
pointer plus a compile-time offset, and a static_assert proves the span covers the last one before
anything runs. A module that keeps its state in a file-static still compiles and still passes its
tests - it just carries per-module BSS and proves nothing.

The transformation:

  static FooCtx s_foo;                ->   #define FOO_OFF_CTX 0u
                                           static_assert(FOO_OFF_CTX + sizeof(FooCtx) <= FOO_BORROW, ...)
                                           #define FOO_CTX(w) ((FooCtx *)(void *)((w) + FOO_OFF_CTX))

  s_foo.field                         ->   FOO_CTX(work)->field

A helper that touches the context needs the borrow to reach it with, and how it gets one depends on
who calls it. A helper this file calls gains `uint8_t *restrict work` as its first parameter, and
its call sites gain the argument. A helper whose address is taken is a callback: its signature
belongs to whoever dispatches it, so it takes the span from the accessor instead and refuses a null
the way every entry does.
"""

import re

from codemask import code_mask

STATIC_CTX = re.compile(r"^static\s+(?P<type>\w+)\s+(?P<name>s_\w+)\s*;\s*$", re.M)
FUNC = re.compile(r"^(?P<head>(?:static\s+)?[A-Za-z_][\w \t\*]*?\b(?P<name>\w+)\s*\((?P<params>[^;{]*)\))\s*\{", re.M)


def find_context(src):
    """The module's own context: the file-static whose type is not the span holder."""
    for m in STATIC_CTX.finditer(src):
        if m.group("type").endswith("OwnCtx"):
            continue  # the span holder this tool generates
        return m
    return None


def _body(src, f, mask):
    """The half-open range inside the braces of the function FUNC matched as f."""
    i, depth = f.end(), 1
    while i < len(src) and depth:
        if mask[i]:
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
        i += 1
    return f.end(), i - 1


def _funcs(src, mask):
    return [f for f in FUNC.finditer(src) if mask[f.start()]]


def _address_taken(src, name, mask, defstart):
    """True when the name appears somewhere other than a call: its address is being passed."""
    for m in re.finditer(r"(?<![\w.>])%s(?![\w])" % re.escape(name), src):
        if not mask[m.start()] or m.start() == defstart:
            continue
        j = m.end()
        while j < len(src) and src[j] in " \t\r\n":
            j += 1
        if j >= len(src) or src[j] != "(":
            return True
    return False


def _thread(src, name, mask):
    """Give the definition the borrow as its first parameter, and every call the argument."""
    edits = []
    f = None
    for g in _funcs(src, mask):
        if g.group("name") == name:
            f = g
            break
    if f is None:
        return src
    ps, pe = f.start("params"), f.end("params")
    empty = f.group("params").strip() in ("", "void")
    edits.append((ps, pe, "uint8_t *restrict work" if empty else "uint8_t *restrict work, " + f.group("params")))

    for m in re.finditer(r"(?<![\w.>])%s\s*\(" % re.escape(name), src):
        if not mask[m.start()] or m.start() == f.start("name"):
            continue
        j = m.end()
        while j < len(src) and (not mask[j] or src[j] in " \t\r\n"):
            j += 1
        edits.append((m.end(), m.end(), "work" if (j < len(src) and src[j] == ")") else "work, "))

    for a, b, text in sorted(edits, reverse=True):
        src = src[:a] + text + src[b:]
    return src


def _take_span(src, name, module, mask, notes):
    """A callback keeps its signature and takes the span from the accessor."""
    f = None
    for g in _funcs(src, mask):
        if g.group("name") == name:
            f = g
            break
    if f is None:
        return src
    ret = f.group("head").split(name)[0].replace("static", "").strip()
    lead = (
        "\n    // The signature belongs to whoever dispatches this, so the borrow comes from the\n"
        "    // accessor rather than a parameter.\n"
        "    uint8_t *restrict work = protocore_%s_span();\n" % module
    )
    if ret == "void":
        lead += "    if (work == NULL)\n    {\n        return;\n    }\n"
    else:
        notes.append("%s returns %s: give its null-span refusal a value" % (name, ret))
    return src[: f.end()] + lead + src[f.end() :]


def _drop_void_work(src, PRE, mask):
    """`(void)work;` marks a borrow the body never reads. The funnel just gave it one."""
    out, cut = src, []
    for f in _funcs(src, mask):
        a, b = _body(src, f, mask)
        body = src[a:b]
        if ("%s_CTX(work)" % PRE) not in body:
            continue
        m = re.search(r"[ \t]*\(void\)work;[ \t]*\r?\n", body)
        if m:
            cut.append((a + m.start(), a + m.end()))
    for a, b in sorted(cut, reverse=True):
        out = out[:a] + out[b:]
    return out


def funnel(src, module, borrow, notes):
    m = find_context(src)
    if not m:
        notes.append("no file-static context to funnel")
        return src
    ctype, cname = m.group("type"), m.group("name")
    PRE = module.upper()

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
    ) % (PRE, PRE, ctype, borrow, borrow, PRE, ctype, PRE)

    src = src[: m.start()] + block + src[m.end() :]

    # every reach into the context now goes through the borrow
    src = re.sub(r"(?<![\w.>])%s\s*\." % re.escape(cname), "%s_CTX(work)->" % PRE, src)
    src = re.sub(r"(?<![\w.>])&%s(?![\w])" % re.escape(cname), "%s_CTX(work)" % PRE, src)

    # a helper that reaches the context needs the borrow to reach it with
    mask = code_mask(src)
    touched = []
    for f in _funcs(src, mask):
        a, b = _body(src, f, mask)
        if ("%s_CTX(work)" % PRE) in src[a:b] and "work" not in f.group("params"):
            touched.append((f.group("name"), _address_taken(src, f.group("name"), mask, f.start("name"))))

    threaded, callbacks = [], []
    for name, is_cb in touched:
        mask = code_mask(src)
        if is_cb:
            src = _take_span(src, name, module, mask, notes)
            callbacks.append(name)
        else:
            src = _thread(src, name, mask)
            threaded.append(name)

    src = _drop_void_work(src, PRE, code_mask(src))

    if threaded:
        notes.append("helpers given the borrow: " + ", ".join(sorted(set(threaded))))
    if callbacks:
        notes.append("callbacks take the span from the accessor: " + ", ".join(sorted(set(callbacks))))
    return src
