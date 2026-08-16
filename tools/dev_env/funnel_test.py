"""Self-test for funnel.py: the context lands in the borrow, and helpers reach it the right way."""

import sys

sys.path.insert(0, __file__.rsplit("funnel_test.py", 1)[0])
from funnel import funnel  # noqa: E402

SRC = """typedef struct
{
    int n;
    int is_client[4];
} FooCtx;
static FooCtx s_foo;

static void cb_hit(uint8_t ws_id)
{
    s_foo.is_client[ws_id] = 1;
}

static int helper(int x)
{
    return s_foo.n + x;
}

static void foo_begin(uint8_t *restrict work)
{
    (void)work;
    s_foo.n = helper(2);
    on_ws("/p", cb_hit);
}
"""

FAIL = 0


def check(name, cond):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


notes = []
out = funnel(SRC, "foo", "PROTOCORE_FOO_BORROW", notes)
print(out)
print("notes:", notes)
print()

check("offset emitted", "#define FOO_OFF_CTX 0u" in out)
check("assert against the borrow", "sizeof(FooCtx) <= PROTOCORE_FOO_BORROW" in out)
check("region macro emitted", "#define FOO_CTX(w)" in out)
check("the file-static is gone", "static FooCtx s_foo;" not in out)
check("reads go through the borrow", "FOO_CTX(work)->n" in out)

check("plain helper takes the borrow", "static int helper(uint8_t *restrict work, int x)" in out)
check("its call site passes it", "helper(work, 2)" in out)

check("callback keeps its signature", "static void cb_hit(uint8_t ws_id)" in out)
check("callback takes the span", "uint8_t *restrict work = protocore_foo_span();" in out)
check("callback refuses a null span", "if (work == NULL)" in out)
check("its address is still passed bare", 'on_ws("/p", cb_hit);' in out)

check("(void)work dropped where work is read", "(void)work;" not in out)

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
