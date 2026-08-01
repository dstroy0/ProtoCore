// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mnt.cpp
 * @brief The mount registry and the built-in RAM backend.
 *
 * The RAM backend is a fixed pool of named in-BSS files and a fixed handle table; everything is
 * bounded and host-identical. Directories are a flag on a name-table entry rather than a tree: the
 * table already holds whole paths, so "what is in this directory" is a prefix scan of names that
 * are already there. A tree would add nodes, links, and a second lifetime to maintain in order to
 * answer a question the flat table already answers.
 */

#include "server/filesystem/mnt.h"

#include <string.h>

// --- the HAL: which store is mounted -------------------------------------------------------------
// Ungated, because mnt.h declares it ungated: this is the seam every caller reads through, and the
// filesystem accessor calls pc_mnt_active() on every operation whether or not the RAM disk is built.
// Gating it with PC_ENABLE_MNT left the accessor with an unresolved symbol in the default build - a
// library that compiled cleanly file by file and then would not link.
//
// The cost of keeping it is one pointer. The RAM disk below is what has a footprint, and it is what
// the flag gates.
namespace
{
struct MntCtx
{
    const pc_mnt_backend *backend = nullptr;
};
static MntCtx s_hal;
} // namespace

void pc_mnt_mount(const pc_mnt_backend *backend)
{
    s_hal.backend = backend;
}

const pc_mnt_backend *pc_mnt_active(void)
{
    return s_hal.backend;
}

// --- the RAM disk: the part with a footprint ------------------------------------------------------
#if PC_ENABLE_MNT

namespace
{
struct RamFile
{
    bool is_dir;
    char name[PC_MNT_NAME_MAX];
    size_t len;
    uint8_t data[PC_MNT_RAM_FILE_SIZE];
};
struct RamHandle
{
    bool open;
    bool is_dir;
    int file;   ///< index into rf[]; for a directory cursor, -1 means the root
    size_t pos; ///< file: byte offset. directory: the next rf[] index to examine.
    pc_mnt_mode mode;
};

// All RAM-disk state, owned by one instance (internal linkage): the file pool and the handle table,
// grouped so it is one named owner, unreachable from any other translation unit. (The RAM ops are
// fixed-signature vtable entries, so they reach this single owner directly.)
struct RamCtx
{
    // Which pool entries hold a file, one bit each. Occupancy is a single bit, so the whole pool's
    // answer fits in a register: a free entry is one bit scan instead of a walk.
    uint32_t used;
    RamFile rf[PC_MNT_RAM_FILES];
    RamHandle rh[PC_MNT_MAX_OPEN];
};
static RamCtx s_mnt;

static_assert(PC_MNT_RAM_FILES > 0 && PC_MNT_RAM_FILES <= 32,
              "the RAM pool's occupancy is one 32-bit word, one bit per file");
#define PC_MNT_RAM_BITS ((uint32_t)(((uint64_t)1 << PC_MNT_RAM_FILES) - 1u))

bool ram_used(int i)
{
    return (s_mnt.used & (1u << i)) != 0;
}

int ram_find(const char *name)
{
    for (int i = 0; i < PC_MNT_RAM_FILES; i++)
    {
        if (ram_used(i) && strncmp(s_mnt.rf[i].name, name, PC_MNT_NAME_MAX) == 0)
        {
            return i;
        }
    }
    return -1;
}

int ram_create(const char *name, bool is_dir)
{
    if (strnlen(name, PC_MNT_NAME_MAX + 1) >= PC_MNT_NAME_MAX)
    {
        return -1;
    }
    uint32_t free_bits = ~s_mnt.used & PC_MNT_RAM_BITS;
    if (free_bits == 0)
    {
        return -1;
    }
    int i = (int)__builtin_ctz(free_bits);
    s_mnt.used |= (1u << i);
    s_mnt.rf[i].is_dir = is_dir;
    strncpy(s_mnt.rf[i].name, name, PC_MNT_NAME_MAX - 1);
    s_mnt.rf[i].name[PC_MNT_NAME_MAX - 1] = '\0';
    s_mnt.rf[i].len = 0;
    return i;
}

int ram_alloc_handle(void)
{
    for (int h = 0; h < PC_MNT_MAX_OPEN; h++)
    {
        if (!s_mnt.rh[h].open)
        {
            return h;
        }
    }
    return -1;
}

bool ram_handle_ok(int h)
{
    return h >= 0 && h < PC_MNT_MAX_OPEN && s_mnt.rh[h].open;
}

// The directory a cursor is walking, as a name prefix. The root is not a table entry - it always
// exists and owns every path - so it answers "/" without one.
const char *ram_dirpath(const RamHandle *h)
{
    return (h->file < 0) ? "/" : s_mnt.rf[h->file].name;
}

// True if @p name lies directly in @p prefix (one level down, not deeper); @p rest gets the entry's
// own name. The root prefix is "/" and carries its own separator; any other prefix needs one.
bool ram_child_of(const char *name, const char *prefix, const char **rest)
{
    size_t plen = strnlen(prefix, PC_MNT_NAME_MAX);
    if (strncmp(name, prefix, plen) != 0)
    {
        return false;
    }
    const char *tail = name + plen;
    if (plen > 1) // a non-root prefix does not include the separator
    {
        if (tail[0] != '/')
        {
            return false;
        }
        tail++;
    }
    if (tail[0] == '\0' || strchr(tail, '/') != nullptr)
    {
        return false; // the prefix itself, or something deeper than one level
    }
    *rest = tail;
    return true;
}

int ram_open(const char *path, int mode)
{
    if (path == nullptr)
    {
        return -1;
    }
    const pc_mnt_mode m = static_cast<pc_mnt_mode>(mode); // the backend ABI carries mode as int
    int f = ram_find(path);
    if (f >= 0 && s_mnt.rf[f].is_dir)
    {
        return -1; // a directory is opened with opendir
    }
    if (m == pc_mnt_mode::PC_MNT_READ)
    {
        if (f < 0)
        {
            return -1;
        }
    }
    else
    {
        if (f < 0)
        {
            f = ram_create(path, false);
        }
        if (f < 0)
        {
            return -1;
        }
        if (m == pc_mnt_mode::PC_MNT_WRITE)
        {
            s_mnt.rf[f].len = 0;
        }
    }
    int h = ram_alloc_handle();
    if (h < 0)
    {
        return -1; // handle pool exhausted
    }
    s_mnt.rh[h].open = true;
    s_mnt.rh[h].is_dir = false;
    s_mnt.rh[h].file = f;
    s_mnt.rh[h].mode = m;
    s_mnt.rh[h].pos = (m == pc_mnt_mode::PC_MNT_APPEND) ? s_mnt.rf[f].len : 0;
    return h;
}

int ram_read(int h, void *buf, size_t n)
{
    if (!ram_handle_ok(h) || s_mnt.rh[h].is_dir)
    {
        return -1;
    }
    RamFile *f = &s_mnt.rf[s_mnt.rh[h].file];
    size_t avail = (s_mnt.rh[h].pos < f->len) ? (f->len - s_mnt.rh[h].pos) : 0;
    size_t k = n < avail ? n : avail;
    memcpy(buf, f->data + s_mnt.rh[h].pos, k);
    s_mnt.rh[h].pos += k;
    return static_cast<int>(k);
}

int ram_write(int h, const void *buf, size_t n)
{
    if (!ram_handle_ok(h) || s_mnt.rh[h].is_dir || s_mnt.rh[h].mode == pc_mnt_mode::PC_MNT_READ)
    {
        return -1;
    }
    RamFile *f = &s_mnt.rf[s_mnt.rh[h].file];
    size_t cap = (s_mnt.rh[h].pos < PC_MNT_RAM_FILE_SIZE) ? (PC_MNT_RAM_FILE_SIZE - s_mnt.rh[h].pos) : 0;
    size_t k = n < cap ? n : cap;
    memcpy(f->data + s_mnt.rh[h].pos, buf, k);
    s_mnt.rh[h].pos += k;
    if (s_mnt.rh[h].pos > f->len)
    {
        f->len = s_mnt.rh[h].pos;
    }
    return static_cast<int>(k);
}

void ram_close(int h)
{
    if (h >= 0 && h < PC_MNT_MAX_OPEN)
    {
        s_mnt.rh[h].open = false;
    }
}

bool ram_seek(int h, uint64_t off)
{
    if (!ram_handle_ok(h) || s_mnt.rh[h].is_dir || off > PC_MNT_RAM_FILE_SIZE)
    {
        return false;
    }
    s_mnt.rh[h].pos = static_cast<size_t>(off);
    return true;
}

long ram_size(const char *path)
{
    int f = ram_find(path);
    return (f < 0 || s_mnt.rf[f].is_dir) ? -1 : static_cast<long>(s_mnt.rf[f].len);
}

bool ram_exists(const char *path)
{
    return ram_find(path) >= 0;
}

// Delete one file. mnt is blind: it does not know what a subtree is, because it does not know what a
// path means. Removing a directory and its members is the accessor's operation (pc_fs_remove), built
// out of these per-node calls.
bool ram_remove(const char *path)
{
    int f = ram_find(path);
    if (f < 0 || s_mnt.rf[f].is_dir)
    {
        return false;
    }
    s_mnt.used &= ~(1u << f);
    return true;
}

bool ram_rename(const char *from, const char *to)
{
    if (from == nullptr || to == nullptr || strnlen(to, PC_MNT_NAME_MAX + 1) >= PC_MNT_NAME_MAX)
    {
        return false;
    }
    int f = ram_find(from);
    if (f < 0)
    {
        return false;
    }
    int dst = ram_find(to);
    if (dst >= 0)
    {
        s_mnt.used &= ~(1u << dst); // overwrite an existing destination
    }
    strncpy(s_mnt.rf[f].name, to, PC_MNT_NAME_MAX - 1);
    s_mnt.rf[f].name[PC_MNT_NAME_MAX - 1] = '\0';
    return true;
}

bool ram_mkdir(const char *path)
{
    if (path == nullptr || ram_find(path) >= 0)
    {
        return false; // already exists
    }
    return ram_create(path, true) >= 0;
}

bool ram_rmdir(const char *path)
{
    int d = ram_find(path);
    if (d < 0 || !s_mnt.rf[d].is_dir)
    {
        return false;
    }
    for (int i = 0; i < PC_MNT_RAM_FILES; i++)
    {
        const char *rest = nullptr;
        if (i != d && ram_used(i) && ram_child_of(s_mnt.rf[i].name, s_mnt.rf[d].name, &rest))
        {
            return false; // not empty
        }
    }
    s_mnt.used &= ~(1u << d);
    return true;
}

// The RAM pool keeps no clock, so mtime is 0 - which pc_mnt_stat states as the contract. A listing
// then formats a stable epoch rather than an invented time.
void ram_fill_stat(const RamFile *f, pc_mnt_stat *out)
{
    out->is_dir = f->is_dir;
    out->size = f->is_dir ? 0 : static_cast<uint64_t>(f->len);
    out->mtime = 0;
}

bool ram_stat(const char *path, pc_mnt_stat *out)
{
    if (path == nullptr || out == nullptr)
    {
        return false;
    }
    if (path[0] == '/' && path[1] == '\0') // the root always exists and is a directory
    {
        out->is_dir = true;
        out->size = 0;
        out->mtime = 0;
        return true;
    }
    int f = ram_find(path);
    if (f < 0)
    {
        return false;
    }
    ram_fill_stat(&s_mnt.rf[f], out);
    return true;
}

int ram_opendir(const char *path)
{
    if (path == nullptr)
    {
        return -1;
    }
    int d = -1; // the root
    if (!(path[0] == '/' && path[1] == '\0'))
    {
        d = ram_find(path);
        if (d < 0 || !s_mnt.rf[d].is_dir)
        {
            return -1;
        }
    }
    int h = ram_alloc_handle();
    if (h < 0)
    {
        return -1;
    }
    s_mnt.rh[h].open = true;
    s_mnt.rh[h].is_dir = true;
    s_mnt.rh[h].file = d;
    s_mnt.rh[h].pos = 0;
    return h;
}

bool ram_readdir(int h, pc_mnt_stat *out, char *name, size_t name_cap)
{
    if (!ram_handle_ok(h) || !s_mnt.rh[h].is_dir || out == nullptr || name == nullptr || name_cap == 0)
    {
        return false;
    }
    const char *prefix = ram_dirpath(&s_mnt.rh[h]);
    for (size_t i = s_mnt.rh[h].pos; i < PC_MNT_RAM_FILES; i++)
    {
        const char *rest = nullptr;
        if (!ram_used((int)i) || !ram_child_of(s_mnt.rf[i].name, prefix, &rest))
        {
            continue;
        }
        size_t rl = strnlen(rest, name_cap);
        if (rl >= name_cap)
        {
            continue; // the caller's buffer cannot hold this name - skip it rather than truncate
        }
        memcpy(name, rest, rl);
        name[rl] = '\0';
        ram_fill_stat(&s_mnt.rf[i], out);
        s_mnt.rh[h].pos = i + 1;
        return true;
    }
    s_mnt.rh[h].pos = PC_MNT_RAM_FILES;
    return false;
}

const pc_mnt_backend s_ram_backend = {ram_open,   ram_read,   ram_write, ram_close, ram_seek, ram_size,    ram_exists,
                                      ram_remove, ram_rename, ram_mkdir, ram_rmdir, ram_stat, ram_opendir, ram_readdir};
} // namespace

const pc_mnt_backend *pc_mnt_ram(void)
{
    return &s_ram_backend;
}

void pc_mnt_ram_format(void)
{
    s_mnt.used = 0; // the whole pool, one store rather than a loop
    for (int h = 0; h < PC_MNT_MAX_OPEN; h++)
    {
        s_mnt.rh[h].open = false;
    }
}

#endif // PC_ENABLE_MNT
