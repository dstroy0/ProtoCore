// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file guardrails.c
 * @brief Heap/stack guardrail evaluator + JSON (pure) and the live sampler. See guardrails.h.
 */

#include "server/core/guardrails/guardrails.h"
#include "mmgr/membuild/membuild.h"  // protocore_sb frame builder
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#if PROTOCORE_ENABLE_GUARDRAILS

/** @brief The guardrails' compile-time storage: the breach callback, for the length of a build. */
struct GuardrailsStorage
{
    protocore_breach_fn cb;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define GUARDRAILS_OFF_CTX 0u
static_assert(GUARDRAILS_OFF_CTX + sizeof(struct GuardrailsStorage) <= PROTOCORE_GUARDRAILS_BORROW,
              "PROTOCORE_GUARDRAILS_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define GUARDRAILS_CTX(w) ((struct GuardrailsStorage *)(void *)((w) + GUARDRAILS_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_GUARDRAILS_BORROW persistent bytes, or null while the pool was short
} GuardrailsOwnCtx;
static GuardrailsOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_guardrails_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_GUARDRAILS_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void guardrails_eval(uint8_t *restrict work)
{
    (void)work;
    const protocore_health *h = Guardrails.health;
    const uint32_t heap_min = Guardrails.floors.heap_min;
    const uint32_t frag_min_block = Guardrails.floors.frag_min_block;
    const uint32_t stack_min = Guardrails.floors.stack_min;

    uint8_t b = PROTOCORE_BREACH_NONE;
    if (!h)
    {
        Guardrails.breaches = b;
        return;
    }
    if (h->free_heap < heap_min)
    {
        b |= PROTOCORE_BREACH_HEAP;
    }
    if (h->largest_free_block < frag_min_block)
    {
        b |= PROTOCORE_BREACH_FRAG;
    }
    if (h->stack_free < stack_min)
    {
        b |= PROTOCORE_BREACH_STACK;
    }
    Guardrails.breaches = b;
}

static void guardrails_json(uint8_t *restrict work)
{
    (void)work;
    const protocore_health *h = Guardrails.health;
    char *out = Guardrails.out.out;
    const size_t cap = Guardrails.out.cap;

    Guardrails.n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!h)
    {
        return;
    }
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, "{\"free_heap\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)h->free_heap));
    Sb.put(&sb_out, ",\"min_free_heap\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)h->min_free_heap));
    Sb.put(&sb_out, ",\"largest_free_block\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)h->largest_free_block));
    Sb.put(&sb_out, ",\"stack_free\":");
    Sb.u32(&sb_out, (uint32_t)((unsigned)h->stack_free));
    Sb.put(&sb_out, "}");
    const int w = (int)Sb.finish(&sb_out);
    // w < 0 is unreachable: this format is all %u (unsigned) with literal text, no
    // multibyte/wide-character conversion, which is the only way snprintf goes negative.
    if (!sb_out.ok)
    {
        out[0] = '\0';
        return;
    }
    Guardrails.n = w;
}

static void guardrails_sample(uint8_t *restrict work)
{
    (void)work;
    protocore_health *h = Guardrails.health;
    if (!h)
    {
        return;
    }
#if PROTOCORE_HAS_VENDOR_HEAP_INFO
    h->free_heap = protocore_platform_heap_free();
    h->min_free_heap = protocore_platform_heap_min_free();
    h->largest_free_block = protocore_platform_heap_max_alloc();
    h->stack_free = protocore_platform_stack_free();
#else
    const protocore_health blank = {0};
    *h = blank;
#endif
}

static void guardrails_begin(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    GUARDRAILS_CTX(work)->cb = Guardrails.cb;
}

static void guardrails_check(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    protocore_health h;
    Guardrails.health = &h;
    guardrails_sample(work);
    Guardrails.health = &h;
    Guardrails.floors.heap_min = PROTOCORE_GUARDRAIL_HEAP_MIN;
    Guardrails.floors.frag_min_block = PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK;
    Guardrails.floors.stack_min = PROTOCORE_GUARDRAIL_STACK_MIN;
    guardrails_eval(work);
    if (Guardrails.breaches != PROTOCORE_BREACH_NONE && GUARDRAILS_CTX(work)->cb)
    {
        GUARDRAILS_CTX(work)->cb(Guardrails.breaches, &h);
    }
    Guardrails.health = NULL;
}

// Designated, so a member's position in the struct does not decide what it binds to.
GuardrailsNs Guardrails = {.eval = guardrails_eval,
                           .json = guardrails_json,
                           .sample = guardrails_sample,
                           .begin = guardrails_begin,
                           .check = guardrails_check};

#endif // PROTOCORE_ENABLE_GUARDRAILS
