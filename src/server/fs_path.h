// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fs_path.h
 * @brief Shared filesystem path helpers for the file-transfer servers (SFTP / SCP, and the pattern the
 *        static file server + WebDAV also use): join a mount root with a request subpath and reject `..`
 *        traversal - the single choke point that keeps a request from escaping its mount.
 *
 * Pure string logic (no fs::FS, no heap, no stdlib) so it is host-testable and identical on device + host.
 * The guard is a substring reject (not realpath/symlink resolution) - the on-flash filesystems (FAT /
 * LittleFS) have no symlinks, so a `..`-free joined path cannot escape the root.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FS_PATH_H
#define PROTOCORE_FS_PATH_H

#include "protocore_config.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** @brief Join a filesystem @p root and a @p sub path into @p out, normalizing the separator. @return false on
 * overflow. */
inline bool fs_path_join(const char *root, const char *sub, char *out, size_t cap)
{
    size_t rlen = strnlen(root, cap);
    bool root_slash = (rlen > 0 && root[rlen - 1] == '/');
    if (root_slash && sub[0] == '/')
    {
        sub++;
    }
    bool sub_slash = (sub[0] == '/');
    const char *sep = (root_slash || sub_slash) ? "" : "/";
    pc_sb sb_out = {out, cap, 0, true};
    pc_sb_put(&sb_out, root);
    pc_sb_put(&sb_out, sep);
    pc_sb_put(&sb_out, sub);
    int wn = (int)pc_sb_finish(&sb_out);
    // wn <= 0 is unreachable, for both halves of "<= 0": wn < 0 only comes from an encoding error,
    // which "%s" of plain non-null C strings cannot raise (see file_serving.cpp's identical guard);
    // wn == 0 would need root, sep, AND sub all empty simultaneously, but sep is "" only when
    // root_slash or sub_slash is true, and both of those require a nonempty contributing string
    // (a nonempty root, or a sub that still starts with '/' after the sub++ skip) - so the formatted
    // result is never shorter than 1 byte.
    return wn > 0 && wn < (int)cap; // GCOVR_EXCL_BR_LINE  wn <= 0 unreachable (see above)
}

/**
 * @brief Resolve a mount @p root + a request @p sub path to an on-disk path in @p out: reject any `..`
 *        traversal, join onto the root, and drop a trailing '/'.
 * @return 0 on success, -1 on a traversal attempt (`..` present), -2 if the joined path would overflow @p out.
 */
inline int fs_path_resolve(const char *root, const char *sub, char *out, size_t cap)
{
    if (strstr(sub, ".."))
    {
        return -1; // path traversal - refuse before touching the filesystem
    }
    if (!fs_path_join(root, sub, out, cap))
    {
        return -2;
    }
    size_t fpl = strnlen(out, cap);
    if (fpl > 1 && out[fpl - 1] == '/')
    {
        out[fpl - 1] = '\0';
    }
    return 0;
}

#endif // PROTOCORE_FS_PATH_H
