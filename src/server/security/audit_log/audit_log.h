// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file audit_log.h
 * @brief Tamper-evident, hash-chained audit log (PROTOCORE_ENABLE_AUDIT_LOG).
 *
 * An append-only security log where each entry carries
 * `hash = SHA-256(prev_hash || seq || ts || category || msg)`, chaining every
 * record to its predecessor. Altering, reordering, or deleting any retained
 * record breaks the chain, which protocore_audit_verify() detects in O(n). All
 * storage is a fixed RAM ring of PROTOCORE_AUDIT_LOG_ENTRIES records (no heap,
 * bounded latency); when it wraps, the evicted record's hash becomes a moving
 * chain anchor, so the *retained window* still verifies end-to-end.
 *
 * **Durability / forwarding.** The RAM ring is only the recent window for query
 * and verification. Install a sink with protocore_audit_set_sink() to forward every
 * record, at the moment it is created (before it can ever be evicted), to a
 * durable or remote store - an SD-card file, a syslog / HTTP log service, a
 * serial console. Because the sink receives the full record including its chain
 * hash, the external store preserves the same tamper-evident chain. Use
 * protocore_audit_format() to render a record as one JSON line for that sink.
 *
 * Pure and host-tested (the chain is the same on host and ESP32; SHA-256 comes
 * from protocore_sha256, hardware-accelerated on ESP32). Single-accessor like the log
 * buffer: append from one context (a worker / loop), not concurrently.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AUDIT_LOG_H
#define PROTOCORE_AUDIT_LOG_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_AUDIT_LOG

PROTOCORE_BEGIN_DECLS

// PROTOCORE_AUDIT_LOG_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums it
// into its arena. A caller takes them once and passes the pointer to every call. How they are
// carved is this module's and is never named here.

#define PROTOCORE_AUDIT_HASH_LEN 32

/** @brief Standard audit event categories (extend with your own values). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_AUDIT_SYSTEM = 0,    ///< Boot, shutdown, time change, generic.
    PROTOCORE_AUDIT_AUTH = 1,      ///< Authentication success (login).
    PROTOCORE_AUDIT_AUTH_FAIL = 2, ///< Authentication failure.
    PROTOCORE_AUDIT_ACCESS = 3,    ///< Resource access (request served / denied).
    PROTOCORE_AUDIT_CONFIG = 4,    ///< Configuration change.
    PROTOCORE_AUDIT_ADMIN = 5,     ///< Privileged / administrative action.
} protocore_audit_cat;

/** @brief One audit record. seq is monotonic and never reused across evictions. */
typedef struct
{
    uint32_t seq;                           ///< Monotonic sequence number (1-based).
    uint32_t ts;                            ///< Timestamp from protocore_millis() at append.
    protocore_audit_cat category;           ///< audit category (a ::protocore_audit_cat, or a user value cast in).
    char msg[PROTOCORE_AUDIT_MSG_LEN];      ///< Null-terminated message (truncated).
    uint8_t hash[PROTOCORE_AUDIT_HASH_LEN]; ///< SHA-256(prev_hash || fields).
} protocore_audit_entry;

/** @brief Sink invoked once per record, at append time, for durable forwarding. */
typedef void (*protocore_audit_sink_fn)(const protocore_audit_entry *entry);

/** @brief What set_sink takes. */
typedef struct
{
    protocore_audit_sink_fn sink;
} AuditLogSetSinkArgs;

/** @brief What append takes. */
typedef struct
{
    protocore_audit_cat category;
    const char *msg;
} AuditLogAppendArgs;

/** @brief What at takes. */
typedef struct
{
    uint16_t i;
} AuditLogAtArgs;

/** @brief What verify takes. */
typedef struct
{
    uint32_t *first_broken_seq;
} AuditLogVerifyArgs;

/** @brief What cat_name takes. */
typedef struct
{
    protocore_audit_cat category;
} AuditLogCatNameArgs;

/** @brief What format takes. */
typedef struct
{
    const protocore_audit_entry *entry;
    char *out;
    size_t cap;
} AuditLogFormatArgs;

/** @brief What dump_json takes. */
typedef struct
{
    char *out;
    size_t cap;
} AuditLogDumpJsonArgs;
typedef struct
{
    AuditLogSetSinkArgs set_sink_args;
    AuditLogAppendArgs append_args;
    AuditLogAtArgs at_args;
    AuditLogVerifyArgs verify_args;
    AuditLogCatNameArgs cat_name_args;
    AuditLogFormatArgs format_args;
    AuditLogDumpJsonArgs dump_json_args;

    proto_bool ok;
    uint32_t ms;
    uint16_t value;
    const protocore_audit_entry *ptr;
    const char *text;
    int n;

    void (*const reset)(uint8_t *restrict work);
    void (*const set_sink)(uint8_t *restrict work);
    void (*const append)(uint8_t *restrict work);
    void (*const count)(uint8_t *restrict work);
    void (*const at)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
    void (*const cat_name)(uint8_t *restrict work);
    void (*const format)(uint8_t *restrict work);
    void (*const dump_json)(uint8_t *restrict work);
} AuditLogNs;

/** @brief The one symbol this module exports. */
extern AuditLogNs AuditLog;

/**
 * @brief The PROTOCORE_AUDIT_LOG_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_audit_log_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AUDIT_LOG

#endif // PROTOCORE_AUDIT_LOG_H
