// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SSH_SSH_H
#define PROTOCORE_SSH_SSH_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ssh.h
 * @brief Every byte the connections use, one span per slot.
 *
 * @c work is PROTOCORE_SSH_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint8_t *(*conn_slot)(uint8_t *restrict, uint8_t);
} SshNs;
PROTOCORE_NS_LAYOUT(SshNs, conn_slot);

/**
 * @brief The base of slot i's span, or NULL when i is out of range. Every .
 * @param work PROTOCORE_SSH_BORROW bytes the caller took. Not held past the call.
 * @param i I
 * @return The uint8_t *.
 */
uint8_t *protocore_ssh_conn_slot(uint8_t *restrict work, uint8_t i);

/**
 * @brief The PROTOCORE_SSH_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_ssh_span(void);

/** @brief Module namespace. */
PROTOCORE_NS SshNs Ssh PROTOCORE_UNUSED = {.conn_slot = protocore_ssh_conn_slot};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SSH_SSH_H
