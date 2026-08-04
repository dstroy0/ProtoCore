// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file guardrails.c
 * @brief Heap/stack guardrail evaluator + JSON (pure) and the ESP32 sampler.
 *
 * The evaluator and serializer are host-tested; the sample reads the live esp_* /
 * FreeRTOS counters on ESP32 and returns zeros on host.
 */

#include "services/security/guardrails/guardrails.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_GUARDRAILS

#include <stdio.h>

#if PROTOCORE_HOT
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif
uint8_t pc_guardrail_eval(const pc_health *h, uint32_t heap_min, uint32_t frag_min_block, uint32_t stack_min)
{
    uint8_t b = PC_BREACH_NONE;
    if (!h)
    {
        return b;
    }
    if (h->free_heap < heap_min)
    {
        b |= PC_BREACH_HEAP;
    }
    if (h->largest_free_block < frag_min_block)
    {
        b |= PC_BREACH_FRAG;
    }
    if (h->stack_free < stack_min)
    {
        b |= PC_BREACH_STACK;
    }
    return b;
}

int pc_health_json(const pc_health *h, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!h)
    {
        return 0;
    }
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, "{\"free_heap\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)h->free_heap));
    pc_sb_put(&sb_out, ",\"min_free_heap\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)h->min_free_heap));
    pc_sb_put(&sb_out, ",\"largest_free_block\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)h->largest_free_block));
    pc_sb_put(&sb_out, ",\"stack_free\":");
    pc_sb_u32(&sb_out, (uint32_t)((unsigned)h->stack_free));
    pc_sb_put(&sb_out, "}");
    int w = (int)pc_sb_finish(&sb_out);
    // w < 0 is unreachable: this format is all %u (unsigned) with literal text, no
    // multibyte/wide-character conversion, which is the only way snprintf goes negative.
    if (!sb_out.ok) // GCOVR_EXCL_BR_LINE  w<0 half unreachable (see above)
    {
        out[0] = '\0';
        return 0;
    }
    return w;
}

#if PROTOCORE_HOT

// All guardrails sampler state, owned by one instance (internal linkage): the breach
// callback, so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    pc_breach_fn cb;
} GuardrailsCtx;
static GuardrailsCtx s_gr;

void pc_guardrails_sample(pc_health *h)
{
    if (!h)
    {
        return;
    }
    h->free_heap = esp_get_free_heap_size();
    h->min_free_heap = esp_get_minimum_free_heap_size();
    h->largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    h->stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
}

void pc_guardrails_begin(pc_breach_fn cb)
{
    s_gr.cb = cb;
}

uint8_t pc_guardrails_check(void)
{
    pc_health h;
    pc_guardrails_sample(&h);
    uint8_t b = pc_guardrail_eval(&h, PC_GUARDRAIL_HEAP_MIN, PC_GUARDRAIL_FRAG_MIN_BLOCK, PC_GUARDRAIL_STACK_MIN);
    if (b != PC_BREACH_NONE && s_gr.cb)
    {
        s_gr.cb(b, &h);
    }
    return b;
}

#else // host build - no live counters

void pc_guardrails_sample(pc_health *h)
{
    if (h)
    {
        const pc_health blank = {0};
        *h = blank;
    }
}
void pc_guardrails_begin(pc_breach_fn cb)
{
    (void)cb;
}
uint8_t pc_guardrails_check(void)
{
    return PC_BREACH_NONE;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_GUARDRAILS
