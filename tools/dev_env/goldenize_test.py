"""Self-test for goldenize.py: the doc prose it moves, and the two things it takes back out.

Each case here is a defect the tool shipped once: a module brief cut mid-clause and left with a
dangling "...", a trailing comment stranded on a dead `return;`, and `(void)work;` announcing that
an entry never touches the borrow in an entry that threads it into every helper it calls.
"""

import sys

sys.path.insert(0, __file__.rsplit("goldenize_test.py", 1)[0])
from goldenize import drop_void_work, first_sentence, land_returns  # noqa: E402

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

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
