// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file guardrails.h
 * @brief Runtime heap/stack guardrails (PROTOCORE_ENABLE_GUARDRAILS).
 *
 * Samples the live health of the device - free heap, the heap low-water mark, the
 * largest free block (a fragmentation signal), and the calling task's remaining
 * stack - and trips a guardrail (callback) when any value crosses its configured
 * floor. A proactive fail-safe hook on top of the passive numbers in /metrics: an
 * app can shed load, drop to a safe state, or reboot before exhaustion bites.
 *
 * The threshold evaluator and the JSON serializer are pure and host-tested; the
 * sample reads `esp_get_free_heap_size` / `heap_caps_get_largest_free_block` /
 * `uxTaskGetStackHighWaterMark` on ESP32 (zeros on host).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GUARDRAILS_H
#define PROTOCORE_GUARDRAILS_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_GUARDRAILS

PROTOCORE_BEGIN_DECLS

/** @brief A health snapshot. */
typedef struct
{
    uint32_t free_heap;          ///< current free heap (bytes).
    uint32_t min_free_heap;      ///< lowest free heap since boot (bytes).
    uint32_t largest_free_block; ///< largest allocatable block (fragmentation, bytes).
    uint32_t stack_free;         ///< calling task's remaining stack (bytes).
} protocore_health;

/** @brief Guardrail breach flags: a bitmask OR'd together, so integer constants in a namespacing
 *  struct (cast-free at every | / &). */
#define PROTOCORE_BREACH_NONE 0
#define PROTOCORE_BREACH_HEAP 1  ///< free heap below PROTOCORE_GUARDRAIL_HEAP_MIN.
#define PROTOCORE_BREACH_FRAG 2  ///< largest block below PROTOCORE_GUARDRAIL_FRAG_MIN_BLOCK.
#define PROTOCORE_BREACH_STACK 4 ///< task stack remaining below PROTOCORE_GUARDRAIL_STACK_MIN.

/** @brief Breach callback: @p breaches is a PROTOCORE_BREACH_* bitmask, @p h the snapshot. */
typedef void (*protocore_breach_fn)(uint8_t breaches, const protocore_health *h);

/** @brief The floors an evaluation judges a snapshot against. */
typedef struct
{
    uint32_t heap_min;       ///< free heap under this trips PROTOCORE_BREACH_HEAP
    uint32_t frag_min_block; ///< largest block under this trips PROTOCORE_BREACH_FRAG
    uint32_t stack_min;      ///< stack remaining under this trips PROTOCORE_BREACH_STACK
} GuardrailFloorArgs;

/** @brief Where a serialize writes. */
typedef struct
{
    char *out;  ///< the buffer the JSON lands in
    size_t cap; ///< how much room it has
} GuardrailOutArgs;

/**
 * @brief The device's live health, and the floors it is judged against.
 *
 * A caller sets the members a call takes, invokes it through ::Guardrails, and reads the outcome
 * off the same handle.
 *
 * @var GuardrailsNs::health    the snapshot a call fills, judges, or serializes
 * @var GuardrailsNs::floors    what an eval judges against
 * @var GuardrailsNs::out       where a json writes
 * @var GuardrailsNs::cb        the callback a begin installs
 * @var GuardrailsNs::breaches  the PROTOCORE_BREACH_* bitmask an eval or a check reports
 * @var GuardrailsNs::n         characters a json wrote, 0 when it did not fit
 * @var GuardrailsNs::eval      judge @ref health against @ref floors
 * @var GuardrailsNs::json      serialize @ref health
 * @var GuardrailsNs::sample    fill @ref health from the live counters
 * @var GuardrailsNs::begin     install the breach callback
 * @var GuardrailsNs::check     sample, judge against PROTOCORE_GUARDRAIL_*, fire on a breach
 */
typedef struct
{
    protocore_health *health;
    GuardrailFloorArgs floors;
    GuardrailOutArgs out;
    protocore_breach_fn cb;
    uint8_t breaches;
    int n;
} GuardrailsVars;

/** @brief The operands and the outcome. */
extern GuardrailsVars GuardrailsV;

/** @brief The entries. */
typedef struct
{
    void (*const eval)(uint8_t *restrict work);
    void (*const json)(uint8_t *restrict work);
    void (*const sample)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const check)(uint8_t *restrict work);
} GuardrailsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in GuardrailsV or a region of the borrow at a fixed offset.
void protocore_guardrails_eval(uint8_t *restrict work);
void protocore_guardrails_json(uint8_t *restrict work);
void protocore_guardrails_sample(uint8_t *restrict work);
void protocore_guardrails_begin(uint8_t *restrict work);
void protocore_guardrails_check(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Guardrails.eval(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const GuardrailsNs Guardrails __attribute__((unused)) = {
    .eval = protocore_guardrails_eval,
    .json = protocore_guardrails_json,
    .sample = protocore_guardrails_sample,
    .begin = protocore_guardrails_begin,
    .check = protocore_guardrails_check,
};

/**
 * @brief The PROTOCORE_GUARDRAILS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_guardrails_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GUARDRAILS

#endif // PROTOCORE_GUARDRAILS_H
