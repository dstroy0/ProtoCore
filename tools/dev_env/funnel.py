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

# The module's context, with or without an initializer. Matching only the bare `static T s_x;`
# form reported a module with an initialized context as holding no state at all, which produced a
# conversion that left the context in BSS and carved no borrow.
# `static SimaticCtx s_ctx;` and `static struct TlsStorage s_store;` are the same thing: the
# module's own context. The Ns path already read both spellings, and the flat path reading only the
# typedef'd one made a module with the struct spelling scan as holding no state at all.
# An ALL_CAPS token between `static` and the type is a placement attribute (PSRAM, a section), not
# the type. Reading it as one made every context that carries one invisible here - and those are the
# big ones, which is the whole reason a module gets a placement attribute in the first place.
STATIC_CTX = re.compile(
    r"^static\s+(?:(?P<attr>[A-Z_][A-Z0-9_]*)\s+)?(?P<type>(?:struct\s+)?\w+)\s+(?P<name>s_\w+)\s*"
    r"(?P<init>=[^;]*)?;\s*$",
    re.M | re.S,
)
FUNC = re.compile(r"^(?P<head>(?:static\s+)?[A-Za-z_][\w \t\*]*?\b(?P<name>\w+)\s*\((?P<params>[^;{]*)\))\s*\{", re.M)


def find_context(src):
    """The module's own context: the file-static that is neither the span holder nor the handle."""
    for m in STATIC_CTX.finditer(src):
        t = m.group("type")
        if t.endswith("OwnCtx"):
            continue  # the span holder this tool generates
        if t.endswith("Internal"):
            continue  # the handle the conversion deletes, not state the module keeps
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


def _shared_body_start(src, f, name):
    """Where the body a callback's arms SHARE begins.

    A vendor callback whose signature changed between SDK versions is written as two heads in
    `#if` / `#else` arms over one body: each arm opens a brace, the `#endif` closes the choice, and
    the statements after it are compiled either way. espnow's `on_recv` is that shape. A
    declaration put after one arm's brace is invisible to the other and duplicated in the first, so
    the borrow goes after the `#endif`. Returns f.end() for the ordinary one-head function.
    """
    m = re.compile(r"^[ \t]*#\s*(else|elif)\b[^\n]*\n", re.M).search(src, f.end())
    if not m or "}" in src[f.end() : m.start()]:
        return f.end()
    e = re.compile(r"^[ \t]*#\s*endif\b[^\n]*\n", re.M).search(src, m.end())
    if not e or not re.search(r"(?<![\w])%s\s*\(" % re.escape(name), src[m.end() : e.start()]):
        return f.end()
    return e.end() - 1


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
    at = _shared_body_start(src, f, name)
    return src[:at] + lead + src[at:]


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


def funnel(src, module, borrow, notes, stated=()):
    m = find_context(src)
    if not m:
        notes.append("no file-static context to funnel")
        return src
    # A borrow arrives zeroed, so an initialized context has a default the move would silently
    # drop. Where that default belongs - an init entry, a first-use path, a constant the entries
    # read - is the module's call, not this tool's, so it refuses and says what it saw.
    if m.group("init"):
        # A designated initializer names its members, so what the caller already seats can be
        # subtracted and only the rest refused. A positional one names nothing, so all of it is
        # unstated and the whole initializer is reported.
        seeded = set(re.findall(r"\.(\w+)\s*=", m.group("init")))
        missing = sorted(seeded - set(stated)) if seeded else [m.group("name")]
        if missing:
            notes.append(
                "REFUSED: %s carries an initializer (%s) and a borrow arrives zeroed. State the "
                'defaults in the spec as "defaults": {"%s": "<value>"} so the carve seats them, then '
                "re-run." % (m.group("name"), " ".join(m.group("init").split())[:80], missing[0])
            )
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

    # A function that reaches the context needs the borrow to reach it with - and so does a
    # function that only passes the borrow on to one that does. Threading a helper puts `work` in
    # its callers, so the scan repeats until no body names a `work` it was never given; one pass
    # left the callers of a threaded helper referencing an identifier that is not there.
    threaded, callbacks = [], []
    for _ in range(64):
        mask = code_mask(src)
        touched = []
        for f in _funcs(src, mask):
            a, b = _body(src, f, mask)
            if f.group("name") in threaded or f.group("name") in callbacks:
                continue
            if re.search(r"(?<![\w.>])work(?![\w])", src[a:b]) and "work" not in f.group("params"):
                touched.append((f.group("name"), _address_taken(src, f.group("name"), mask, f.start("name"))))
        if not touched:
            break
        # A function written as two heads in `#if` / `#else` arms is yielded once per arm, and
        # threading it twice declares the borrow twice in one body.
        touched = list(dict.fromkeys(touched))
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
