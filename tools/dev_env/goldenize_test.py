"""Self-test for goldenize.py: the doc prose it moves, and the two things it takes back out.

Each case here is a defect the tool shipped once: a module brief cut mid-clause and left with a
dangling "...", a trailing comment stranded on a dead `return;`, and `(void)work;` announcing that
an entry never touches the borrow in an entry that threads it into every helper it calls.
"""

import sys

sys.path.insert(0, __file__.rsplit("goldenize_test.py", 1)[0])
from goldenize import (  # noqa: E402
    drop_self_assign,
    drop_void_work,
    dropped_names,
    enclosing_has_work,
    first_sentence,
    land_returns,
    module_inlines,
    module_macros,
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
check("a brief with no full stop is kept whole", first_sentence("A brief with no full stop"), "A brief with no full stop")
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
check("a doc tag is stripped and the newlines collapse", first_sentence("@brief  Does\n a thing. More."), "Does a thing")

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
check("an entry that threads work into a helper drops the cast", drop_void_work(USES_WORK), USES_WORK.replace("    (void)work;\n", "", 1))
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
    module_macros('/** first. */\n#define A 1\n\n/** second. */\n#define B 2\n', ""),
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

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
