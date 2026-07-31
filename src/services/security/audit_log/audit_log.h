// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file audit_log.h
 * @brief Tamper-evident, hash-chained audit log (PC_ENABLE_AUDIT_LOG).
 *
 * An append-only security log where each entry carries
 * `hash = SHA-256(prev_hash || seq || ts || category || msg)`, chaining every
 * record to its predecessor. Altering, reordering, or deleting any retained
 * record breaks the chain, which pc_audit_verify() detects in O(n). All
 * storage is a fixed RAM ring of PC_AUDIT_LOG_ENTRIES records (no heap,
 * bounded latency); when it wraps, the evicted record's hash becomes a moving
 * chain anchor, so the *retained window* still verifies end-to-end.
 *
 * **Durability / forwarding.** The RAM ring is only the recent window for query
 * and verification. Install a sink with pc_audit_set_sink() to forward every
 * record, at the moment it is created (before it can ever be evicted), to a
 * durable or remote store - an SD-card file, a syslog / HTTP log service, a
 * serial console. Because the sink receives the full record including its chain
 * hash, the external store preserves the same tamper-evident chain. Use
 * pc_audit_format() to render a record as one JSON line for that sink.
 *
 * Pure and host-tested (the chain is the same on host and ESP32; SHA-256 comes
 * from pc_sha256, hardware-accelerated on ESP32). Single-accessor like the log
 * buffer: append from one context (a worker / loop), not concurrently.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AUDIT_LOG_H
#define PROTOCORE_AUDIT_LOG_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_AUDIT_LOG

/** @brief SHA-256 chain-hash length per record. */
#define PC_AUDIT_HASH_LEN 32

/** @brief Standard audit event categories (extend with your own values). */
enum class pc_audit_cat : uint8_t
{
    PC_AUDIT_SYSTEM = 0,    ///< Boot, shutdown, time change, generic.
    PC_AUDIT_AUTH = 1,      ///< Authentication success (login).
    PC_AUDIT_AUTH_FAIL = 2, ///< Authentication failure.
    PC_AUDIT_ACCESS = 3,    ///< Resource access (request served / denied).
    PC_AUDIT_CONFIG = 4,    ///< Configuration change.
    PC_AUDIT_ADMIN = 5,     ///< Privileged / administrative action.
};

/** @brief One audit record. seq is monotonic and never reused across evictions. */
struct pc_audit_entry
{
    uint32_t seq;                    ///< Monotonic sequence number (1-based).
    uint32_t ts;                     ///< Timestamp from pc_millis() at append.
    pc_audit_cat category;           ///< audit category (a ::pc_audit_cat, or a user value cast in).
    char msg[PC_AUDIT_MSG_LEN];      ///< Null-terminated message (truncated).
    uint8_t hash[PC_AUDIT_HASH_LEN]; ///< SHA-256(prev_hash || fields).
};

/** @brief Sink invoked once per record, at append time, for durable forwarding. */
typedef void (*pc_audit_sink_fn)(const pc_audit_entry *entry);

/** @brief Clear the ring, reset the sequence counter and the chain anchor to genesis. */
void pc_audit_reset(void);

/**
 * @brief Forward every future record to @p sink at append time (nullptr to detach).
 *
 * The sink runs synchronously inside pc_audit_append(); keep it short and
 * non-reentrant (do not call pc_audit_append() from it).
 */
void pc_audit_set_sink(pc_audit_sink_fn sink);

/**
 * @brief Append a record and return its sequence number.
 *
 * @param category  ::pc_audit_cat or a user-defined value.
 * @param msg       Null-terminated message (truncated to PC_AUDIT_MSG_LEN-1).
 * @return the assigned monotonic sequence number.
 */
uint32_t pc_audit_append(pc_audit_cat category, const char *msg);

/** @brief Number of records currently retained in the ring (0 .. PC_AUDIT_LOG_ENTRIES). */
uint16_t pc_audit_count(void);

/** @brief Record @p i (0 = oldest retained .. count-1 = newest), or nullptr if out of range. */
const pc_audit_entry *pc_audit_at(uint16_t i);

/**
 * @brief Recompute the chain over the retained window and report integrity.
 *
 * @param first_broken_seq  if non-null, set to the seq of the first record whose
 *                          hash does not match when the chain is broken.
 * @return true if every retained record verifies against its predecessor.
 */
bool pc_audit_verify(uint32_t *first_broken_seq);

/** @brief Human-readable name for a standard ::pc_audit_cat ("system" for unknown). */
const char *pc_audit_cat_name(pc_audit_cat category);

/**
 * @brief Render one record as a JSON object (hash as full 64-char hex).
 * @return characters written (excluding NUL), or 0 if @p cap is too small.
 */
int pc_audit_format(const pc_audit_entry *entry, char *out, size_t cap);

/**
 * @brief Dump the retained window as a JSON document for an endpoint.
 *
 * `{"intact":bool,"count":N,"entries":[ {record}, ... ]}` (plus "first_broken"
 * when the chain is broken).
 *
 * @return characters written (excluding NUL), or 0 if @p cap is too small.
 */
int pc_audit_dump_json(char *out, size_t cap);

#endif // PC_ENABLE_AUDIT_LOG
#endif // PROTOCORE_AUDIT_LOG_H
