// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Evaluate @p h against the floors; returns a PROTOCORE_BREACH_* bitmask. */
uint8_t protocore_guardrail_eval(const protocore_health *h, uint32_t heap_min, uint32_t frag_min_block,
                                 uint32_t stack_min);

/**
 * @brief Serialize a health snapshot as JSON into @p out.
 * @return characters written, or 0 if @p cap is too small (fail-closed).
 */
int protocore_health_json(const protocore_health *h, char *out, size_t cap);

// ---------------------------------------------------------------------------
// Sampling + guardrail check (ESP32; zeros / no-op on host)
// ---------------------------------------------------------------------------

/** @brief Fill @p h from the live esp_* / FreeRTOS counters (zeros on host). */
void protocore_guardrails_sample(protocore_health *h);

/** @brief Breach callback: @p breaches is a PROTOCORE_BREACH_* bitmask, @p h the snapshot. */
typedef void (*protocore_breach_fn)(uint8_t breaches, const protocore_health *h);

/** @brief Install the breach callback (thresholds come from PROTOCORE_GUARDRAIL_*). */
void protocore_guardrails_begin(protocore_breach_fn cb);

/**
 * @brief Sample, evaluate, and fire the callback if any guardrail is breached.
 * @return the PROTOCORE_BREACH_* bitmask (0 = all clear).
 */
uint8_t protocore_guardrails_check(void);

#endif // PROTOCORE_ENABLE_GUARDRAILS

PROTOCORE_END_DECLS

#endif // PROTOCORE_GUARDRAILS_H
