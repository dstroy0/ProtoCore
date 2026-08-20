// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mnt_ram.h
 * @brief The built-in RAM filesystem backend (PROTOCORE_ENABLE_MNT).
 *
 * A fixed pool of named in-BSS files and a fixed handle table: bounded, zero-heap, and
 * host-identical, which is what lets the file-transfer servers run under a native test. It
 * implements the @ref protocore_mnt_backend contract that server/storage/mnt registers, and it is
 * one of several things that can - a board layer wraps a real fs::FS the same way.
 *
 * Directories are a flag on a name-table entry rather than a tree: the table already holds whole
 * paths, so "what is in this directory" is a prefix scan of names that are already there. A tree
 * would add nodes, links, and a second lifetime to maintain in order to answer a question the flat
 * table already answers.
 *
 * The module publishes its vtable rather than mounting itself, so the mount stays one decision in
 * one place:
 *
 *     MntRam.backend(work);
 *     MntV.args.backend = MntRamV.backend;
 *     Mnt.mount(work);
 *
 * Pool dimensions are PROTOCORE_MNT_RAM_FILES, PROTOCORE_MNT_RAM_FILE_SIZE, PROTOCORE_MNT_MAX_OPEN
 * and PROTOCORE_MNT_NAME_MAX in protocore_config.h.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MNT_RAM_H
#define PROTOCORE_MNT_RAM_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MNT

#include "server/storage/mnt/mnt.h" // protocore_mnt_backend: the contract this implements

PROTOCORE_BEGIN_DECLS

/**
 * @brief What the backend reports.
 *
 * @var MntRamVars::backend  the vtable to hand ::Mnt, never NULL once @ref MntRamNs::backend ran
 */
typedef struct
{
    const protocore_mnt_backend *backend;
} MntRamVars;

/** @brief The operands and the outcome. */
extern MntRamVars MntRamV;

/**
 * @brief The RAM filesystem.
 *
 * A caller invokes an entry through ::MntRam and reads the outcome off @ref MntRamV. The pool and
 * the handle table are behind @ref internal.
 *
 * @var MntRamNs::backend  publish the vtable, for the caller to mount
 * @var MntRamNs::format   empty the pool and close every handle
 */
typedef struct
{
    void (*const backend)(uint8_t *restrict work);
    void (*const format)(uint8_t *restrict work);
} MntRamNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MntRamV.
void protocore_mnt_ram_backend(uint8_t *restrict work);
void protocore_mnt_ram_format(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `MntRam.format(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MntRamNs MntRam __attribute__((unused)) = {
    .backend = protocore_mnt_ram_backend,
    .format = protocore_mnt_ram_format,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MNT

#endif // PROTOCORE_MNT_RAM_H
