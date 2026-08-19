// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh.h
 * @brief Every byte the connections use, one span per slot.
 */

#ifndef PROTOCORE_SSH_SSH_H
#define PROTOCORE_SSH_SSH_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SSH

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SSH_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What conn_slot takes: i. */
typedef struct
{
    uint8_t i;
} SshConnSlotArgs;

/**
 * @brief Every byte the connections use, one span per slot.
 *
 * A caller sets the members a call takes, invokes it through ::Ssh with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ssh.conn_slot_args.i = ...;
 *   Ssh.conn_slot(work);
 *   // Ssh.ptr is what the call reports
 *
 * @var SshNs::conn_slot_args  what conn_slot takes: i
 * @var SshNs::ok  a call's true/false outcome
 * @var SshNs::ptr  the pointer a call reports
 * @var SshNs::conn_slot  the base of slot i's span, or NULL when i is out of range. Every ...
 *
 * @c work is PROTOCORE_SSH_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    SshConnSlotArgs conn_slot_args;

    proto_bool ok;
    uint8_t *ptr;

    void (*const conn_slot)(uint8_t *restrict work);
} SshNs;

/** @brief The one symbol this module exports. */
extern SshNs Ssh;

/**
 * @brief The PROTOCORE_SSH_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ssh_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH

#endif // PROTOCORE_SSH_SSH_H
