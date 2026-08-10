// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_mnt_fs.c
 * @brief Mount backend over an Arduino `fs::FS`. See esp_mnt_fs.h.
 *
 * Maps the mount vtable onto a real filesystem, with a fixed pool of fs::File objects standing in
 * for the small-int handles the vtable hands out. A directory cursor is one of those same handles -
 * the framework's directory reader is an fs::File too - so open and opendir draw from one pool and
 * close releases either.
 */

#include "core_setup/hal/esp/esp_mnt_fs.h"

#if PC_ENABLE_MNT && defined(ARDUINO)

#include <FS.h>

// The bound filesystem plus the open-handle pool, owned by one instance (internal linkage). The FS
// ops are fixed-signature vtable entries, so they reach this single owner directly.
typedef struct
{
    fs::FS *fs = NULL;
    fs::File file[PC_MNT_MAX_OPEN];
    proto_bool used[PC_MNT_MAX_OPEN];
} EspMntFsCtx;
static EspMntFsCtx s_mnt_fs;

static int slot_alloc(void)
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

static proto_bool slot_ok(int h)
{
    return h >= 0 && h < PC_MNT_MAX_OPEN && s_mnt_fs.used[h];
}

// The framework reports an entry's whole path; a directory listing wants the entry's own name.
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

static void fill_stat(fs::File &f, pc_mnt_stat *out)
{
    out->is_dir = f.isDirectory();
    out->size = out->is_dir ? 0 : (uint64_t)(f.size());
    out->mtime = (uint32_t)(f.getLastWrite());
}

static int fs_open(const char *path, int mode)
{
    if (s_mnt_fs.fs == NULL || path == NULL)
    {
        return -1;
    }
    const pc_mnt_mode md = (pc_mnt_mode)(mode); // ABI int -> enum
    const char *m = (md == PC_MNT_WRITE) ? FILE_WRITE : (md == PC_MNT_APPEND) ? FILE_APPEND : FILE_READ;
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
    s_mnt_fs.used[h] = PROTO_TRUE;
    return h;
}

static int fs_read(int h, void *buf, size_t n)
{
    if (!slot_ok(h))
    {
        return -1;
    }
    return (int)(s_mnt_fs.file[h].read((uint8_t *)(buf), n));
}

static int fs_write(int h, const void *buf, size_t n)
{
    if (!slot_ok(h))
    {
        return -1;
    }
    return (int)(s_mnt_fs.file[h].write((const uint8_t *)(buf), n));
}

static void fs_close(int h)
{
    if (slot_ok(h))
    {
        s_mnt_fs.file[h].close();
        s_mnt_fs.file[h] = fs::File();
        s_mnt_fs.used[h] = PROTO_FALSE;
    }
}

static proto_bool fs_seek(int h, uint64_t off)
{
    if (!slot_ok(h))
    {
        return PROTO_FALSE;
    }
    return s_mnt_fs.file[h].seek((uint32_t)(off));
}

static long fs_size(const char *path)
{
    if (s_mnt_fs.fs == NULL)
    {
        return -1;
    }
    fs::File f = s_mnt_fs.fs->open(path, FILE_READ);
    if (!f)
    {
        return -1;
    }
    long sz = (long)(f.size());
    f.close();
    return sz;
}

static proto_bool fs_exists(const char *path)
{
    return s_mnt_fs.fs != NULL && s_mnt_fs.fs->exists(path);
}

static proto_bool fs_remove(const char *path)
{
    return s_mnt_fs.fs != NULL && s_mnt_fs.fs->remove(path);
}

static proto_bool fs_rename(const char *from, const char *to)
{
    return s_mnt_fs.fs != NULL && s_mnt_fs.fs->rename(from, to);
}

static proto_bool fs_mkdir(const char *path)
{
    return s_mnt_fs.fs != NULL && s_mnt_fs.fs->mkdir(path);
}

static proto_bool fs_rmdir(const char *path)
{
    return s_mnt_fs.fs != NULL && s_mnt_fs.fs->rmdir(path);
}

static proto_bool fs_stat(const char *path, pc_mnt_stat *out)
{
    if (s_mnt_fs.fs == NULL || out == NULL)
    {
        return PROTO_FALSE;
    }
    fs::File f = s_mnt_fs.fs->open(path, FILE_READ);
    if (!f)
    {
        return PROTO_FALSE;
    }
    fill_stat(f, out);
    f.close();
    return PROTO_TRUE;
}

static int fs_opendir(const char *path)
{
    if (s_mnt_fs.fs == NULL || path == NULL)
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
    s_mnt_fs.used[h] = PROTO_TRUE;
    return h;
}

static proto_bool fs_readdir(int h, pc_mnt_stat *out, char *name, size_t name_cap)
{
    if (!slot_ok(h) || out == NULL || name == NULL || name_cap == 0)
    {
        return PROTO_FALSE;
    }
    fs::File c = s_mnt_fs.file[h].openNextFile();
    if (!c)
    {
        return PROTO_FALSE; // end of directory
    }
    const char *base = base_name(c.name());
    size_t bl = strnlen(base, name_cap);
    if (bl >= name_cap)
    {
        c.close();
        return PROTO_FALSE; // the caller's buffer cannot hold this name
    }
    memcpy(name, base, bl);
    name[bl] = '\0';
    fill_stat(c, out);
    c.close();
    return PROTO_TRUE;
}

static const pc_mnt_backend s_fs_backend = {fs_open,   fs_read,   fs_write, fs_close, fs_seek, fs_size,    fs_exists,
                                            fs_remove, fs_rename, fs_mkdir, fs_rmdir, fs_stat, fs_opendir, fs_readdir};

const pc_mnt_backend *pc_mnt_fs(fs::FS *filesystem)
{
    s_mnt_fs.fs = filesystem;
    for (int h = 0; h < PC_MNT_MAX_OPEN; h++)
    {
        s_mnt_fs.used[h] = PROTO_FALSE;
    }
    return &s_fs_backend;
}

#endif // PC_ENABLE_MNT && ARDUINO
