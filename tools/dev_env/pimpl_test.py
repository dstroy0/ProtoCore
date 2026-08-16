"""Self-test for pimpl.py: an Internal-handle module becomes the golden."""

import sys

sys.path.insert(0, __file__.rsplit("pimpl_test.py", 1)[0])
import pimpl  # noqa: E402

HDR = """#ifndef X_H
#define X_H
#include "protocore_config.h"
#if PROTOCORE_ENABLE_FOO

/** @brief The state and the calls that reach it. */
struct FooInternal;

/**
 * @var FooNs::slot   the slot a call acts on
 * @var FooNs::internal   the state and the calls that reach it
 */
typedef struct
{
    uint8_t slot;
    proto_bool ok;

    void (*init)(struct FooInternal *ctx);
    void (*find)(struct FooInternal *ctx);

    struct FooInternal *internal;
} FooNs;

extern FooNs Foo;

#endif
#endif
"""

SRC = """struct FooStorage
{
    uint8_t table[4];
    uint8_t count;
};

struct FooInternal
{
    struct FooStorage *store;
    FooNs *ns;
    uint16_t frag_size;
};

static struct FooStorage s_store;

static struct FooInternal s_foo = {.store = &s_store, .ns = &Foo, .frag_size = 7};

static void init(struct FooInternal *restrict ctx)
{
    (void)ctx;
    ctx->store->count = 0;
}

static void find(struct FooInternal *restrict ctx)
{
    ctx->ns->ok = ctx->store->count > 0 && ctx->frag_size != 0;
}

FooNs Foo = {.init = init, .find = find, .internal = &s_foo};
"""

FAIL = 0


def check(name, cond):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


spec = {
    "module": "foo",
    "ns": "FooNs",
    "object": "Foo",
    "internal": "FooInternal",
    "storage": "FooStorage",
    "gate": "PROTOCORE_ENABLE_FOO",
    "borrow": "PROTOCORE_FOO_BORROW",
    "entries": ["init", "find"],
}

h = pimpl.convert_header(HDR, spec)
s = pimpl.convert_source(SRC, spec)
print(h)
print("=" * 70)
print(s)

check("header entries take the borrow", "void (*const init)(uint8_t *restrict work);" in h)
check("header drops the handle member", "struct FooInternal *internal;" not in h)
check("header drops the forward declaration", "struct FooInternal;" not in h)
check("header declares the span accessor", "uint8_t *protocore_foo_span(void);" in h)

check("context merges storage and carried state", "uint8_t table[4];" in s and "uint16_t frag_size;" in s)
check("context drops the two indirections", "FooStorage *store;" not in s and "FooNs *ns;" not in s)
check("offset emitted", "#define FOO_OFF_CTX 0u" in s)
check("assert against the borrow", "sizeof(FooCtx) <= PROTOCORE_FOO_BORROW" in s)
check("store reads go through the borrow", "FOO_CTX(work)->count" in s)
check("carried state goes through the borrow", "FOO_CTX(work)->frag_size" in s)
check("ns writes go to the namespace", "Foo.ok =" in s)
check("entry signatures take the borrow", "static void init(uint8_t *restrict work)" in s)
check("(void)ctx becomes (void)work", "(void)work;" in s and "(void)ctx;" not in s)
check("the file-statics are gone", "static struct FooStorage" not in s and "static struct FooInternal" not in s)
check("the namespace drops .internal", ".internal" not in s)
check("the span accessor is defined", "uint8_t *protocore_foo_span(void)" in s)

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
