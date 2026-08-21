// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_MNT_RAM_H
#define PROTOCORE_MNT_RAM_H

#include "protocore_config.h"       // the entry point: protocore_types.h for the widths
#include "server/storage/mnt/mnt.h" // the complete type a public struct below holds by value

PROTOCORE_BEGIN_DECLS

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
 * MntRam.backend(work);
 * MntV.args.backend = MntRamV.backend;
 * Mnt.mount(work);
 *
 * Pool dimensions are PROTOCORE_MNT_RAM_FILES, PROTOCORE_MNT_RAM_FILE_SIZE, PROTOCORE_MNT_MAX_OPEN
 * and PROTOCORE_MNT_NAME_MAX in protocore_config.h.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_MNT_RAM_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    const protocore_mnt_backend *(*backend)(uint8_t *restrict);
    void (*format)(uint8_t *restrict);
} MntRamNs;
PROTOCORE_NS_LAYOUT(MntRamNs, backend, format);

/**
 * @brief Publish the vtable, for the caller to mount.
 * @param work PROTOCORE_MNT_RAM_BORROW bytes the caller took. Not held past the call.
 * @return The const protocore_mnt_backend *.
 */
const protocore_mnt_backend *protocore_mnt_ram_backend(uint8_t *restrict work);
/**
 * @brief Empty the pool and close every handle.
 * @param work PROTOCORE_MNT_RAM_BORROW bytes the caller took. Not held past the call.
 */
void protocore_mnt_ram_format(uint8_t *restrict work);

/** @brief Module namespace. */
PROTOCORE_NS MntRamNs MntRam PROTOCORE_UNUSED = {.backend = protocore_mnt_ram_backend,
                                                 .format = protocore_mnt_ram_format};

PROTOCORE_END_DECLS

#endif // PROTOCORE_MNT_RAM_H
