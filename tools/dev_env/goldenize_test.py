"""Self-test for goldenize.py: the doc prose it moves, and the two things it takes back out.

Each case here is a defect the tool shipped once: a module brief cut mid-clause and left with a
dangling "...", a trailing comment stranded on a dead `return;`, and `(void)work;` announcing that
an entry never touches the borrow in an entry that threads it into every helper it calls.
"""

import sys

sys.path.insert(0, __file__.rsplit("goldenize_test.py", 1)[0])
from goldenize import (  # noqa: E402
    drop_empty_else,
    guard_borrow,
    drop_self_assign,
    drop_void_work,
    dropped_names,
    dropped_tag_decls,
    gutted_macros,
    enclosing_has_work,
    find_gate,
    gate_endif,
    first_sentence,
    land_returns,
    module_externs,
    work_decl_at,
    module_inlines,
    already_converted,
    drop_flat_protos,
    module_macros,
    module_types,
    ns_owns_state,
    rename_handle,
    shape_text,
)

FAIL = 0


def check(name, got, want):
    global FAIL
    ok = got == want
    print(("  ok   " if ok else "  FAIL ") + name)
    if not ok:
        print("         got  %r" % (got,))
        print("         want %r" % (want,))
        FAIL += 1


print("first_sentence: a brief is its opening sentence, whole")
check(
    "the sentence ends at the full stop before the next one",
    first_sentence("TI INA219 monitor codec (PROTOCORE_ENABLE_INA219). The INA219 measures a drop."),
    "TI INA219 monitor codec (PROTOCORE_ENABLE_INA219)",
)
check(
    "a brief with no full stop is kept whole", first_sentence("A brief with no full stop"), "A brief with no full stop"
)
check("one sentence loses only its full stop", first_sentence("One sentence only."), "One sentence only")
check(
    "an abbreviation mid-clause does not end the sentence",
    first_sentence("Handles e.g. the odd case. And then more."),
    "Handles e.g. the odd case",
)
check(
    "a decimal point mid-clause does not end the sentence",
    first_sentence("Ends with version 1.0 inline. Second sentence."),
    "Ends with version 1.0 inline",
)
check(
    "a doc tag is stripped and the newlines collapse", first_sentence("@brief  Does\n a thing. More."), "Does a thing"
)

USES_WORK = """static void mod_read(uint8_t *restrict work)
{
    (void)work;
    int32_t *out = Mod.read_args.out;

    if (!rd16(work, MOD_REG, out))
    {
        Mod.ok = PROTO_FALSE;
    }
}
"""

NO_WORK = """static void mod_scale(uint8_t *restrict work)
{
    (void)work;
    uint16_t raw = Mod.scale_args.raw;

    Mod.value = raw * 4;
}
"""

MENTIONS_WORK_IN_A_COMMENT = """static void mod_pure(uint8_t *restrict work)
{
    (void)work;
    // the borrow this work would need is the caller's
    Mod.value = 1;
}
"""

print("drop_void_work: the cast stays only where it is true")
check(
    "an entry that threads work into a helper drops the cast",
    drop_void_work(USES_WORK),
    USES_WORK.replace("    (void)work;\n", "", 1),
)
check("an entry that never touches the borrow keeps it", drop_void_work(NO_WORK), NO_WORK)
check(
    "a mention inside a comment is not a use",
    drop_void_work(MENTIONS_WORK_IN_A_COMMENT),
    MENTIONS_WORK_IN_A_COMMENT,
)

print("land_returns: the value lands on the member and the dead return goes")
check(
    "a trailing return keeps its comment on the assignment",
    land_returns("\n    return (int32_t)(raw >> 3) * 4; // LSB 4 mV\n", "Mod", "value"),
    "\n    Mod.value = (int32_t)(raw >> 3) * 4; // LSB 4 mV\n",
)
check(
    "a bare trailing return just goes",
    land_returns("\n    return raw * 4;\n", "Mod", "value"),
    "\n    Mod.value = raw * 4;\n",
)
check(
    "an early return keeps its own return, at its indentation",
    land_returns("\n    if (!raw)\n    {\n        return 0;\n    }\n    return raw;\n", "Mod", "value"),
    "\n    if (!raw)\n    {\n        Mod.value = 0;\n        return;\n    }\n    Mod.value = raw;\n",
)
check(
    "the word return inside a comment is prose, not code",
    land_returns("\n    // the last one to return wins\n    return raw;\n", "Mod", "value"),
    "\n    // the last one to return wins\n    Mod.value = raw;\n",
)

# ptp.h publishes `enum protocore_ptp_msg_type { PROTOCORE_PTP_SYNC = 0x0, ... };` - a tagged enum,
# not a typedef. The regenerated header dropped it and the .c that compares against those names
# stopped compiling.
print("module_types: a tagged enum or struct is vocabulary too")
check(
    "a tagged enum is kept",
    module_types("/** the types. */\nenum msg_type\n{\n    SYNC = 0x0,\n    RESP = 0x9\n};\n"),
    ["/** the types. */\nenum msg_type\n{\n    SYNC = 0x0,\n    RESP = 0x9\n};"],
)
check(
    "a tagged struct is kept",
    module_types("struct Foo\n{\n    int a;\n};\n"),
    ["struct Foo\n{\n    int a;\n};"],
)
check(
    "a typedef is still kept",
    module_types("typedef struct\n{\n    int a;\n} Foo;\n"),
    ["typedef struct\n{\n    int a;\n} Foo;"],
)
check("a generated Args shape is still skipped", module_types("typedef struct\n{\n    int a;\n} FooArgs;\n"), [])
check("a forward declaration is not a definition", module_types("struct Foo;\n"), [])

print("module_macros: a macro carries the doc block above it")
check(
    "the block directly above comes with the define",
    module_macros('/** @brief What it is. */\n#define A_URI "x"\n', ""),
    ['/** @brief What it is. */\n#define A_URI "x"'],
)
check(
    "a define with nothing above it is just the define",
    module_macros('#define A_URI "x"\n', ""),
    ['#define A_URI "x"'],
)
check(
    "two documented defines keep their own blocks, not each other's",
    module_macros("/** first. */\n#define A 1\n\n/** second. */\n#define B 2\n", ""),
    ["/** first. */\n#define A 1", "/** second. */\n#define B 2"],
)
check(
    "a block that documents something else does not travel to the define below it",
    module_macros("/** a type. */\ntypedef int T;\n\n#define A 1\n", ""),
    ["#define A 1"],
)
check(
    "the include guard is still skipped, doc block or not",
    module_macros("/** the guard. */\n#define PROTOCORE_X_H\n#define A 1\n", "PROTOCORE_X_H"),
    ["#define A 1"],
)
check(
    "a run whose comment opened a doxygen group carries the group's closer",
    module_macros("/** @name Wire values. */\n///@{\n#define A 1\n#define B 2\n///@}\n", ""),
    ["/** @name Wire values. */\n///@{\n#define A 1\n#define B 2\n///@}"],
)
check(
    "the closer comes back in the spelling the header used",
    module_macros("/** @name V.\n *  @{ */\n#define A 1\n/** @} */\n", ""),
    ["/** @name V.\n *  @{ */\n#define A 1\n/** @} */"],
)
check(
    "a run with no group opener gets no closer invented for it",
    module_macros("/** @brief One. */\n#define A 1\n", ""),
    ["/** @brief One. */\n#define A 1"],
)
check(
    "a contiguous run stays one block, not one block each",
    module_macros("#define A 1\n#define B 2\n#define C 3\n", ""),
    ["#define A 1\n#define B 2\n#define C 3"],
)
check(
    "a blank line ends the run",
    module_macros("#define A 1\n#define B 2\n\n#define C 3\n", ""),
    ["#define A 1\n#define B 2", "#define C 3"],
)
check(
    "a // comment introducing a table comes with it",
    module_macros("// wire values, compared and emitted.\n#define A 1\n#define B 2\n", ""),
    ["// wire values, compared and emitted.\n#define A 1\n#define B 2"],
)
check(
    "several // lines all come",
    module_macros("// one.\n// two.\n#define A 1\n", ""),
    ["// one.\n// two.\n#define A 1"],
)
check(
    "a doxygen line above the // run comes too",
    module_macros("/** the table. */\n// how it is used.\n#define A 1\n", ""),
    ["/** the table. */\n// how it is used.\n#define A 1"],
)
check(
    "a // comment for something else does not travel",
    module_macros("// about the type.\ntypedef int T;\n\n#define A 1\n", ""),
    ["#define A 1"],
)

# A vtable module is matched on the spelling the conversion emits, and a successful rewrite
# restarts the scan at the top of the file. dtls_record's replay_init takes one parameter, so the
# call just written was met again with one argument, staged again, and met again: `convert gen`
# ran for ten minutes on a module with 173 call sites and never finished.
# dtls_conn calls established() above its definition, so it carried
# `static proto_bool protocore_dtls_conn_established(const DtlsConn *c);` near the top under a
# comment explaining why. The definition became `static void dtls_server_established(uint8_t
# *restrict work)`, leaving a declaration of a function that no longer exists.
print("drop_flat_protos: a renamed entry leaves no forward declaration behind")
check(
    "the declaration and the comment over it both go",
    drop_flat_protos(
        "// Called above their definitions.\nstatic proto_bool protocore_x_up(const C *c);\n\nstatic void body(void)\n",
        {"protocore_x_up"},
    ),
    "static void body(void)\n",
)
check(
    "a block declaring something this pass did not rename is left alone",
    drop_flat_protos("static proto_bool other(const C *c);\n", {"protocore_x_up"}),
    "static proto_bool other(const C *c);\n",
)
check(
    "a mixed block keeps the comment the declaration still under it needs",
    drop_flat_protos(
        "// Called above their definitions.\nstatic void protocore_x_up(C *c);\nstatic void other(C *c);\n",
        {"protocore_x_up"},
    ),
    "// Called above their definitions.\nstatic void other(C *c);\n",
)
check(
    "a definition is not a declaration",
    drop_flat_protos("static void protocore_x_up(C *c)\n{\n    return;\n}\n", {"protocore_x_up"}),
    "static void protocore_x_up(C *c)\n{\n    return;\n}\n",
)

# http_route.h opens `#if PROTOCORE_ENABLE_WEBSOCKET` around ONE include and tcp.h opens
# `#if PROTOCORE_NEED_CLIENT` around ONE declaration. Neither is the module's gate, and
# regenerating the whole header under one would drop the module from every build without that
# capability.
# quic_tls.c has `#if PROTOCORE_ENABLE_PQC_KEX` arms through the middle of the file. `$` under
# re.M matches the end of any line, so "an #endif with nothing but whitespace after it" matched the
# first one followed by a blank line: the namespace initializer landed inside the PQC arm, and every
# entry defined below it was bound by nothing.
print("gate_endif: the #endif that closes the file, not an inner arm's")
PQC = "#if PROTOCORE_ENABLE_HTTP3\nint a;\n#if PROTOCORE_ENABLE_PQC\nint b;\n#endif\n\nint c;\n#endif // gate\n"
check("an inner arm's #endif with a blank line after it is not the end", PQC[gate_endif(PQC) :], "#endif // gate\n")
check(
    "trailing whitespace after the last #endif does not hide it",
    "#if X\nint a;\n#endif\n\n\n"[gate_endif("#if X\nint a;\n#endif\n\n\n") :],
    "#endif\n\n\n",
)
check("a source with no conditional at all appends at the end", gate_endif("int a;\n") > len("int a;"), True)

print("find_gate: the condition that wraps the body, or nothing")
GUARD = "#ifndef PROTOCORE_X_H\n#define PROTOCORE_X_H\n\n"
check(
    "a gate around the whole body is the gate",
    find_gate(GUARD + '#if PROTOCORE_ENABLE_X\n#include "a.h"\nvoid f(void);\n#endif\n\n#endif\n'),
    "PROTOCORE_ENABLE_X",
)
check(
    "a compound condition comes back whole",
    find_gate(GUARD + "#if (PROTOCORE_ENABLE_A || PROTOCORE_ENABLE_B)\nvoid f(void);\n#endif\n\n#endif\n"),
    "(PROTOCORE_ENABLE_A || PROTOCORE_ENABLE_B)",
)
check(
    "an inner arm around one include is not a gate",
    find_gate(GUARD + '#if PROTOCORE_ENABLE_WS\n#include "ws.h"\n#endif\n\nvoid f(void);\n\n#endif\n'),
    "",
)
check(
    "nor is the last of several inner arms",
    find_gate(GUARD + "#if PROTOCORE_NEED_CLIENT\nint a;\n#endif\n#if PROTOCORE_NEED_CLIENT\nint b;\n#endif\n\n#endif\n"),
    "",
)
check(
    "a gate with inner arms inside it is still the gate",
    find_gate(
        GUARD + "#if PROTOCORE_ENABLE_X\n#if PROTOCORE_ENABLE_PQC\nint a;\n#endif\nvoid f(void);\n#endif\n\n#endif\n"
    ),
    "PROTOCORE_ENABLE_X",
)
check("a header with no conditional at all has no gate", find_gate(GUARD + "void f(void);\n\n#endif\n"), "")

print("already_converted: the call this pass wrote is not a call to convert")
check("one argument, and it is the span", already_converted(["dtls_record_work"], "dtls_record_work"), True)
check("whitespace either side does not hide it", already_converted([" dtls_record_work "], "dtls_record_work"), True)
check("an accessor span is recognized too", already_converted(["protocore_x_span()"], "protocore_x_span()"), True)
check("one argument that is an operand is a call", already_converted(["&c->replay_ep2"], "dtls_record_work"), False)
check("two arguments are never the converted form", already_converted(["a", "dtls_record_work"], "dtls_record_work"), False)
check("no arguments are not either", already_converted([], "dtls_record_work"), False)


# A self-call is rewritten as `<module>_<entry>(work)`, so the function it sits in has to have one.
# profibus's three telegram parsers are private helpers taking (frame, len, out), and the call
# written into them named an identifier that was not declared.
ENTRY = "static void profibus_build_sd1(uint8_t *restrict work)\n{\n    protocore_pb_fcs(body, 3);\n}\n"
HELPER = "static proto_bool pb_parse_sd1(const uint8_t *frame, size_t len, PbTelegram *out)\n{\n    protocore_pb_fcs(body, 3);\n}\n"
check("an entry has the borrow to pass on", enclosing_has_work(ENTRY, ENTRY.index("protocore_pb_fcs")), True)
check("a private helper does not", enclosing_has_work(HELPER, HELPER.index("protocore_pb_fcs")), False)
check(
    "the helper after an entry is still read as the helper",
    enclosing_has_work(ENTRY + "\n" + HELPER, (ENTRY + "\n" + HELPER).rindex("protocore_pb_fcs")),
    False,
)
check(
    "the entry after a helper is still read as the entry",
    enclosing_has_work(HELPER + "\n" + ENTRY, (HELPER + "\n" + ENTRY).rindex("protocore_pb_fcs")),
    True,
)


# A header's `static inline` helpers emit no external symbol, so the one-symbol rule holds and they
# belong in the regenerated header. Six went missing out of cia402.h and only the linker noticed.
INL = "/// true if bit 10 is set.\nstatic inline proto_bool ok(uint16_t sw)\n{\n    return (sw & 0x400) != 0;\n}\n"
check("a static inline comes across with its comment", module_inlines(INL), [INL.strip()])
check(
    "one in a comment is prose, not a definition",
    module_inlines("/* static inline proto_bool gone(void) { return 0; } */\n"),
    [],
)
check(
    "two in a row are two blocks",
    len(module_inlines(INL + INL.replace("ok(", "ok2("))),
    2,
)
# Asserting only the COUNT here is what let a truncation ship: each block ended at the next `;`
# after its closing brace, so the first swallowed the second's head and thread.h came out with two
# unterminated bodies nested inside each other. Assert the text.
check(
    "each of two in a row is whole",
    module_inlines(INL + INL.replace("ok(", "ok2(")),
    [INL.strip(), INL.replace("ok(", "ok2(").strip()],
)
check(
    "a block ends at its brace, not at the next semicolon",
    module_inlines(INL + "\nuint16_t later(void);\n"),
    [INL.strip()],
)

# The catch-all: whatever the tool failed to carry across is named rather than silently lost.
SPEC = {"entries": [{"flat": "protocore_x_build"}]}
check(
    "a name the regenerated header lost is reported",
    dropped_names(SPEC, "#define KEEP 1\n#define GONE 2\nsize_t protocore_x_build(int a);\n", "#define KEEP 1\n"),
    ["GONE"],
)
check(
    "a converted entry is not reported",
    dropped_names(SPEC, "size_t protocore_x_build(int a);\n", "void (*const build)(uint8_t *restrict work);"),
    [],
)
check(
    "a name that only appears in a comment does not count as defined",
    dropped_names(SPEC, "// #define GONE 2\n", ""),
    [],
)

# A bare tag declaration is neither a #define nor a function, so the catch-all above misses it, and
# the tag survives in the new header as a pointer member's type - declared at member scope.
check(
    "a dropped forward declaration the new header still names is reported",
    dropped_tag_decls("struct ProtoHandler;\n", "    const struct ProtoHandler *ptr;\n"),
    ["ProtoHandler"],
)
check(
    "one the new header carries across is not reported",
    dropped_tag_decls("struct ProtoHandler;\n", "struct ProtoHandler;\n    const struct ProtoHandler *ptr;\n"),
    [],
)
check(
    "one the new header no longer names at all is not reported",
    dropped_tag_decls("struct ProtoHandler;\n", "    size_t n;\n"),
    [],
)
check(
    "a declaration that is only prose does not count",
    dropped_tag_decls("// struct ProtoHandler;\n", "    const struct ProtoHandler *ptr;\n"),
    [],
)

# A macro continued with a trailing backslash: the greedy line match swallowed the backslash, so the
# continuation never matched and the macro came out as its first line - name intact, body gone.
CONT = "#define FLAGS \\\n    (A | B | \\\n     C)\n"
check("a continued macro is captured whole", module_macros(CONT, "")[0].count("\n"), 2)
check("its body survives", "C)" in module_macros(CONT, "")[0], True)
check(
    "a macro that lost its body is reported",
    gutted_macros(CONT, "#define FLAGS\n"),
    ["FLAGS"],
)
check(
    "one that kept its body is not",
    gutted_macros(CONT, CONT),
    [],
)
check(
    "one that was always empty is not reported",
    gutted_macros("#define GUARD\n", "#define GUARD\n"),
    [],
)

# A comment after `return X;` describes X, so it lands on the assignment, not on the bare return.
check(
    "a returned value's comment travels with the value",
    land_returns("    if (a)\n    {\n        return ERR; // no octet\n    }\n    return OK;\n", "D", "frag"),
    "    if (a)\n    {\n        D.frag = ERR; // no octet\n        return;\n    }\n    D.frag = OK;\n",
)

# `return other_entry(...); // why` lands as the call and then Obj.n = Obj.n, which is dead. The
# comment describes the call, so it moves onto it; enip carried two of these.
check(
    "a self-assignment with a trailing comment goes, and the comment rides up",
    drop_self_assign("    enip_build(work);\n    Enip.n = Enip.n; // no command-specific data\n", "Enip"),
    "    enip_build(work); // no command-specific data\n",
)
check(
    "a self-assignment with no comment still just goes",
    drop_self_assign("    enip_build(work);\n    Enip.n = Enip.n;\n", "Enip"),
    "    enip_build(work);\n",
)
check(
    "a real assignment between two members stays",
    drop_self_assign("    Enip.n = Enip.value;\n", "Enip"),
    "    Enip.n = Enip.value;\n",
)


# The handle rename shipped a defect twice, and both times it renamed a struct MEMBER spelled ctx
# while every `->ctx` reader kept the old name, because `>` guarded them.
print("\nrename_handle: ctx is the handle where it is a parameter or an argument, nowhere else")
check(
    "an entry's own parameter is the handle",
    rename_handle("static void f(uint8_t *restrict ctx)\n"),
    "static void f(uint8_t *restrict work)\n",
)
check(
    "so is a helper's, without restrict",
    rename_handle("static void f(uint8_t *ctx, size_t n)\n"),
    "static void f(uint8_t *work, size_t n)\n",
)
check(
    "a bare ctx handed on to a sibling entry is the handle",
    rename_handle("    radio_power(ctx);\n"),
    "    radio_power(work);\n",
)
check(
    "an unrelated struct's `void *ctx;` member is not - UdpBind's handler context",
    rename_handle("typedef struct\n{\n    void *ctx;\n} UdpBind;\n"),
    "typedef struct\n{\n    void *ctx;\n} UdpBind;\n",
)
check(
    "nor is the borrow riding on a marshal record as a member",
    rename_handle("typedef struct\n{\n    uint8_t *ctx;\n} protocore_tcp_call;\n"),
    "typedef struct\n{\n    uint8_t *ctx;\n} protocore_tcp_call;\n",
)
check(
    "a member READ keeps its name too",
    rename_handle("    return do_op(k->ctx);\n"),
    "    return do_op(k->ctx);\n",
)

print("\ndrop_empty_else: a branch the conversion emptied")
check(
    "an #else with nothing under it goes",
    drop_empty_else("#if X\nint a;\n#else\n#endif\n"),
    "#if X\nint a;\n#endif\n",
)
check(
    "so does one holding only blank lines",
    drop_empty_else("#if X\nint a;\n#else\n\n   \n#endif\n"),
    "#if X\nint a;\n#endif\n",
)
check(
    "a comment on the #else line does not make the branch non-empty",
    drop_empty_else("#if X\nint a;\n#else // no state on this arm\n#endif\n"),
    "#if X\nint a;\n#endif\n",
)
check(
    "a branch that still emits code is left alone",
    drop_empty_else("#if X\nint a;\n#else\nint b;\n#endif\n"),
    "#if X\nint a;\n#else\nint b;\n#endif\n",
)
check(
    "so is one holding only a comment line, which is a statement about the arm",
    drop_empty_else("#if X\nint a;\n#else\n// nothing here\n#endif\n"),
    "#if X\nint a;\n#else\n// nothing here\n#endif\n",
)

# The borrow cannot be null: storage comes from the caller, the TU static_asserts that what it was
# handed covers its regions, and the arena sums every borrow before the program is built. A short
# pool is a BUILD failure, and the assert is what we want catching it - a run-time check would only
# hide the thing the assert exists to report. 443 of these were removed from 106 files.
print("\nguard_borrow: no entry checks the borrow, because it cannot be null")
DIRECT = "static void a(uint8_t *restrict work)\n{\n    X.n = FS_CTX(work)->n;\n}\n"
check("a written-out dereference gets no check", guard_borrow(DIRECT, "FS"), DIRECT)
HELPER = "static void a(uint8_t *restrict work)\n{\n    X.ok = store(work) != NULL;\n}\n"
check("nor does one that hands the borrow to a helper", guard_borrow(HELPER, "FS"), HELPER)
VOIDED = "static void a(uint8_t *restrict work)\n{\n    (void)work;\n    X.n = 1;\n}\n"
check("an entry that ignores the borrow is left alone", guard_borrow(VOIDED, "FS"), VOIDED)

print("\nns_owns_state: both shapes of held state")
check(
    "a Storage on the internal handle is state",
    ns_owns_state("struct FooInternal { struct FooStorage *store; };\n"),
    True,
)
check(
    "so is a file-static context beside it",
    ns_owns_state("typedef struct { int n; } FooCtx;\nstatic FooCtx s_foo;\n"),
    True,
)
check(
    "a module with neither holds nothing",
    ns_owns_state("static void foo(uint8_t *restrict work) { (void)work; }\n"),
    False,
)

print("\nmodule_macros: an overridable constant keeps its guard")
check(
    "the #ifndef and #endif come with the define",
    module_macros("/** @brief How long presence is held. */\n#ifndef PROTOCORE_HOLD_MS\n#define PROTOCORE_HOLD_MS 2000\n#endif\n", "PROTOCORE_X_H"),
    ["/** @brief How long presence is held. */\n#ifndef PROTOCORE_HOLD_MS\n"
     "#define PROTOCORE_HOLD_MS 2000\n#endif"],
)
check(
    "a bare define is still taken bare",
    module_macros("/** @brief A wire value. */\n#define PROTOCORE_ACK 6\n", "PROTOCORE_X_H"),
    ["/** @brief A wire value. */\n#define PROTOCORE_ACK 6"],
)
check(
    "an #ifndef naming a different constant does not widen it",
    module_macros("#ifndef PROTOCORE_OTHER\n#define PROTOCORE_ACK 6\n#endif\n", "PROTOCORE_X_H"),
    ["#define PROTOCORE_ACK 6"],
)

# The include guard is the outermost thing in a header. The guard was located with a pattern
# requiring a trailing newline, and the text it was searched in had just been stripped of one, so a
# header whose guard is the LAST thing before the gate did not match: json.h, tcp.h and four others
# came out with `#include "protocore_config.h"` and `#if PROTOCORE_ENABLE_JSON` ABOVE the guard.
# json.h spells its three accessors `PROTOCORE_INLINE`, which is `static inline` behind the
# always_inline attribute. Matching only the spelled-out form dropped all three from the
# regenerated header while eight call sites still used them.
# rsa.h and bignum.h export const data beside their namespace, and regenerating dropped
# tls13_msg's `extern const uint8_t protocore_tls13_hrr_random[32];` while the .c still defined it.
# The namespace's OWN declaration is the generator's to write, and skipping only the unqualified
# form carried `extern const HttpRouteNs HttpRoutes;` through as data - declared twice, once const.
# wamp.c reaches Json only under `#if PROTOCORE_ENABLE_WAMP`, and the nominal borrow was placed
# after the last include OUTSIDE every conditional - above that gate, so 16 bytes of BSS in every
# build with WAMP off. It must not go the other way either: beside an include under an inner
# `#if CAP` it vanishes when that capability is off.
print("\nwork_decl_at: the borrow goes with the calls that pass it")
WAMP = '#include "wamp.h"\n\n#if PROTOCORE_ENABLE_WAMP\n\n#include "json.h"\n\nvoid f(void)\n{\n    Json.init(w);\n}\n\n#endif\n'
check("inside the file's own gate, after the include there", WAMP[work_decl_at(WAMP, "Json") :].startswith("void f"), True)
FLAT = '#include "a.h"\n#include "b.h"\n\nvoid f(void)\n{\n    Json.init(w);\n}\n'
check("a file with no gate takes the last include", FLAT[work_decl_at(FLAT, "Json") :].startswith("void f"), True)
INNER = '#include "a.h"\n\n#if PROTOCORE_ENABLE_X\n#include "x.h"\n#endif\n\nvoid f(void)\n{\n    Json.init(w);\n}\n'
check(
    "an inner arm's include is not where it goes",
    INNER[work_decl_at(INNER, "Json") :].startswith("#if PROTOCORE_ENABLE_X"),
    True,
)

print("\nmodule_externs: the data beside the namespace, but not the namespace")
check(
    "a const array export is kept, with its comment",
    module_externs("/** @brief The HRR random. */\nextern const uint8_t hrr[32];\n", "XNs"),
    ["/** @brief The HRR random. */\nextern const uint8_t hrr[32];"],
)
check("the namespace's own declaration is not data", module_externs("extern XNs X;\n", "XNs"), [])
check("nor is it when spelled const", module_externs("extern const XNs X;\n", "XNs"), [])
check("another module's namespace IS data here", module_externs("extern YNs Y;\n", "XNs"), ["extern YNs Y;"])

print("\nmodule_inlines: PROTOCORE_INLINE is an inline too")
check(
    "a PROTOCORE_INLINE helper is kept, with its comment",
    module_inlines("/** @brief Ok. */\nPROTOCORE_INLINE proto_bool ok(const W *w)\n{\n    return w->ok;\n}\n"),
    ["/** @brief Ok. */\nPROTOCORE_INLINE proto_bool ok(const W *w)\n{\n    return w->ok;\n}"],
)
check(
    "a static inline helper is still kept",
    module_inlines("static inline int f(void)\n{\n    return 1;\n}\n"),
    ["static inline int f(void)\n{\n    return 1;\n}"],
)
check("a plain declaration is not an inline", module_inlines("proto_bool ok(const W *w);\n"), [])

print("\nshape_text: the guard wraps the gate, never the reverse")
GATED_H = (
    "// banner\n\n"
    "/**\n * @file x.h\n */\n\n"
    "#ifndef PROTOCORE_X_H\n#define PROTOCORE_X_H\n\n"
    '#include "protocore_config.h"\n\n'
    "#if PROTOCORE_ENABLE_X\n\n"
    "PROTOCORE_BEGIN_DECLS\n\nvoid f(void);\n\nPROTOCORE_END_DECLS\n\n"
    "#endif // PROTOCORE_ENABLE_X\n\n#endif // PROTOCORE_X_H\n"
)
shaped, _how = shape_text(GATED_H, "x.h", "PROTOCORE_ENABLE_X")
check("the guard still comes first", shaped.index("#ifndef PROTOCORE_X_H") < shaped.index("protocore_config.h"), True)
check("and the gate is under it", shaped.index("protocore_config.h") < shaped.index("#if PROTOCORE_ENABLE_X"), True)
check("the banner is still the first line", shaped.startswith("// banner"), True)

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
