// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_mnt_fs.cpp
 * @brief Mount backend over an Arduino `fs::FS`. See esp_mnt_fs.h.
 *
 * Maps the mount vtable onto a real filesystem, with a fixed pool of fs::File objects standing in
 * for the small-int handles the vtable hands out. A directory cursor is one of those same handles -
 * the framework's directory reader is an fs::File too - so open and opendir draw from one pool and
 * close releases either.
 */

#include "board_drivers/hal/esp/esp_mnt_fs.h"

#if PC_ENABLE_MNT && defined(ARDUINO)

#include <FS.h>
#include <string.h>

namespace
{
// The bound filesystem plus the open-handle pool, owned by one instance (internal linkage). The FS
// ops are fixed-signature vtable entries, so they reach this single owner directly.
struct EspMntFsCtx
{
    fs::FS *fs = nullptr;
    fs::File file[PC_MNT_MAX_OPEN];
    bool used[PC_MNT_MAX_OPEN];
};
static EspMntFsCtx s_mnt_fs;

int slot_alloc(void)
{
    for (int h = 0; h < PC_MNT_MAX_OPEN; h++)
    {
        if (!s_mnt_fs.used[h])
        {
            return h;
        }
    }
    return -1;
}

bool slot_ok(int h)
{
    return h >= 0 && h < PC_MNT_MAX_OPEN && s_mnt_fs.used[h];
}

// The framework reports an entry's whole path; a directory listing wants the entry's own name.
const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash != nullptr) ? slash + 1 : path;
}

void fill_stat(fs::File &f, pc_mnt_stat *out)
{
    out->is_dir = f.isDirectory();
    out->size = out->is_dir ? 0 : static_cast<uint64_t>(f.size());
    out->mtime = static_cast<uint32_t>(f.getLastWrite());
}

int fs_open(const char *path, int mode)
{
    if (s_mnt_fs.fs == nullptr || path == nullptr)
    {
        return -1;
    }
    const pc_mnt_mode md = static_cast<pc_mnt_mode>(mode); // ABI int -> enum
    const char *m = (md == pc_mnt_mode::PC_MNT_WRITE)    ? FILE_WRITE
                    : (md == pc_mnt_mode::PC_MNT_APPEND) ? FILE_APPEND
                                                         : FILE_READ;
    int h = slot_alloc();
    if (h < 0)
    {
        return -1;
    }
    s_mnt_fs.file[h] = s_mnt_fs.fs->open(path, m);
    if (!s_mnt_fs.file[h])
    {
        return -1;
    }
    if (s_mnt_fs.file[h].isDirectory())
    {
        s_mnt_fs.file[h].close(); // a directory is opened with opendir
        return -1;
    }
    s_mnt_fs.used[h] = true;
    return h;
}

int fs_read(int h, void *buf, size_t n)
{
    if (!slot_ok(h))
    {
        return -1;
    }
    return static_cast<int>(s_mnt_fs.file[h].read(static_cast<uint8_t *>(buf), n));
}

int fs_write(int h, const void *buf, size_t n)
{
    if (!slot_ok(h))
    {
        return -1;
    }
    return static_cast<int>(s_mnt_fs.file[h].write(static_cast<const uint8_t *>(buf), n));
}

void fs_close(int h)
{
    if (slot_ok(h))
    {
        s_mnt_fs.file[h].close();
        s_mnt_fs.file[h] = fs::File();
        s_mnt_fs.used[h] = false;
    }
}

bool fs_seek(int h, uint64_t off)
{
    if (!slot_ok(h))
    {
        return false;
    }
    return s_mnt_fs.file[h].seek(static_cast<uint32_t>(off));
}

long fs_size(const char *path)
{
    if (s_mnt_fs.fs == nullptr)
    {
        return -1;
    }
    fs::File f = s_mnt_fs.fs->open(path, FILE_READ);
    if (!f)
    {
        return -1;
    }
    long sz = static_cast<long>(f.size());
    f.close();
    return sz;
}

bool fs_exists(const char *path)
{
    return s_mnt_fs.fs != nullptr && s_mnt_fs.fs->exists(path);
}

bool fs_remove(const char *path)
{
    return s_mnt_fs.fs != nullptr && s_mnt_fs.fs->remove(path);
}

bool fs_rename(const char *from, const char *to)
{
    return s_mnt_fs.fs != nullptr && s_mnt_fs.fs->rename(from, to);
}

bool fs_mkdir(const char *path)
{
    return s_mnt_fs.fs != nullptr && s_mnt_fs.fs->mkdir(path);
}

bool fs_rmdir(const char *path)
{
    return s_mnt_fs.fs != nullptr && s_mnt_fs.fs->rmdir(path);
}

bool fs_stat(const char *path, pc_mnt_stat *out)
{
    if (s_mnt_fs.fs == nullptr || out == nullptr)
    {
        return false;
    }
    fs::File f = s_mnt_fs.fs->open(path, FILE_READ);
    if (!f)
    {
        return false;
    }
    fill_stat(f, out);
    f.close();
    return true;
}

int fs_opendir(const char *path)
{
    if (s_mnt_fs.fs == nullptr || path == nullptr)
    {
        return -1;
    }
    int h = slot_alloc();
    if (h < 0)
    {
        return -1;
    }
    s_mnt_fs.file[h] = s_mnt_fs.fs->open(path, FILE_READ);
    if (!s_mnt_fs.file[h] || !s_mnt_fs.file[h].isDirectory())
    {
        if (s_mnt_fs.file[h])
        {
            s_mnt_fs.file[h].close();
        }
        return -1;
    }
    s_mnt_fs.used[h] = true;
    return h;
}

bool fs_readdir(int h, pc_mnt_stat *out, char *name, size_t name_cap)
{
    if (!slot_ok(h) || out == nullptr || name == nullptr || name_cap == 0)
    {
        return false;
    }
    fs::File c = s_mnt_fs.file[h].openNextFile();
    if (!c)
    {
        return false; // end of directory
    }
    const char *base = base_name(c.name());
    size_t bl = strnlen(base, name_cap);
    if (bl >= name_cap)
    {
        c.close();
        return false; // the caller's buffer cannot hold this name
    }
    memcpy(name, base, bl);
    name[bl] = '\0';
    fill_stat(c, out);
    c.close();
    return true;
}

const pc_mnt_backend s_fs_backend = {fs_open,   fs_read,   fs_write, fs_close, fs_seek, fs_size,    fs_exists,
                                     fs_remove, fs_rename, fs_mkdir, fs_rmdir, fs_stat, fs_opendir, fs_readdir};
} // namespace

const pc_mnt_backend *pc_mnt_fs(fs::FS *filesystem)
{
    s_mnt_fs.fs = filesystem;
    for (int h = 0; h < PC_MNT_MAX_OPEN; h++)
    {
        s_mnt_fs.used[h] = false;
    }
    return &s_fs_backend;
}

#endif // PC_ENABLE_MNT && ARDUINO
