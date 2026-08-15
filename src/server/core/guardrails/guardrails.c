// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file guardrails.c
 * @brief Heap/stack guardrail evaluator + JSON (pure) and the live sampler. See guardrails.h.
 */

#include "server/core/guardrails/guardrails.h"
#include "mmgr/membuild.h" // protocore_sb frame builder

#if PROTOCORE_ENABLE_GUARDRAILS

/** @brief The guardrails' compile-time storage: the breach callback, for the length of a build. */
struct GuardrailsStorage
{
    protocore_breach_fn cb;
};

/**
 * @brief The installed callback and the calls that reach it - what GuardrailsNs points at.
 *
 * @var GuardrailsInternal::store  the breach callback
 * @var GuardrailsInternal::ns     the handle a caller sets a call's members on
 */
struct GuardrailsInternal
{
    struct GuardrailsStorage *store;
    GuardrailsNs *ns;
};

static struct GuardrailsStorage s_store;

static struct GuardrailsInternal s_gr = {.store = &s_store, .ns = &Guardrails};

static void guardrails_eval(struct GuardrailsInternal *restrict ctx)
{
    const protocore_health *h = ctx->ns->health;
    const uint32_t heap_min = ctx->ns->floors.heap_min;
    const uint32_t frag_min_block = ctx->ns->floors.frag_min_block;
    const uint32_t stack_min = ctx->ns->floors.stack_min;

    uint8_t b = PROTOCORE_BREACH_NONE;
    if (!h)
    {
        ctx->ns->breaches = b;
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
    ctx->ns->breaches = b;
}

static void guardrails_json(struct GuardrailsInternal *restrict ctx)
{
    const protocore_health *h = ctx->ns->health;
    char *out = ctx->ns->out.out;
    const size_t cap = ctx->ns->out.cap;

    ctx->ns->n = 0;
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
    ctx->ns->n = w;
}

static void guardrails_sample(struct GuardrailsInternal *restrict ctx)
{
    protocore_health *h = ctx->ns->health;
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

static void guardrails_begin(struct GuardrailsInternal *restrict ctx)
{
    ctx->store->cb = ctx->ns->cb;
}

static void guardrails_check(struct GuardrailsInternal *restrict ctx)
{
    protocore_health h;
    ctx->ns->health = &h;
    guardrails_sample(ctx);
    ctx->ns->health = &h;
    ctx->ns->floors.heap_min = PROTOCORE_GUARDRAIL_HEAP_MIN;
    ctx->ns->floors.frag_min_block = PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK;
    ctx->ns->floors.stack_min = PROTOCORE_GUARDRAIL_STACK_MIN;
    guardrails_eval(ctx);
    if (ctx->ns->breaches != PROTOCORE_BREACH_NONE && ctx->store->cb)
    {
        ctx->store->cb(ctx->ns->breaches, &h);
    }
    ctx->ns->health = NULL;
}

// Designated, so a member's position in the struct does not decide what it binds to.
GuardrailsNs Guardrails = {.eval = guardrails_eval,
                           .json = guardrails_json,
                           .sample = guardrails_sample,
                           .begin = guardrails_begin,
                           .check = guardrails_check,
                           .internal = &s_gr};

#endif // PROTOCORE_ENABLE_GUARDRAILS
