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

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_GUARDRAILS

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

/** @brief The installed callback and the calls that reach it, described only in guardrails.c. */
struct GuardrailsInternal;

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
 * @var GuardrailsNs::internal  the installed callback and the calls that reach it
 */
typedef struct
{
    protocore_health *health;
    GuardrailFloorArgs floors;
    GuardrailOutArgs out;
    protocore_breach_fn cb;

    uint8_t breaches;
    int n;

    void (*eval)(struct GuardrailsInternal *ctx);
    void (*json)(struct GuardrailsInternal *ctx);
    void (*sample)(struct GuardrailsInternal *ctx);
    void (*begin)(struct GuardrailsInternal *ctx);
    void (*check)(struct GuardrailsInternal *ctx);

    struct GuardrailsInternal *internal;
} GuardrailsNs;

/** @brief The one symbol this module exports. */
extern GuardrailsNs Guardrails;

#endif // PROTOCORE_ENABLE_GUARDRAILS

PROTOCORE_END_DECLS

#endif // PROTOCORE_GUARDRAILS_H
