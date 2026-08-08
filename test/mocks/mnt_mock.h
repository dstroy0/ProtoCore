// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// In-memory pc_mnt_backend for the host tests.
//
// The suite used to reach an Arduino `fs::FS` mock, which the file-serving code no longer speaks -
// it takes a pc_mnt_backend now, like every other store. This is that same fixture behind the seam:
// a small registry of path/data/mtime entries the test points at its own string literals, so the
// bytes are borrowed rather than copied and a test can assert on the exact buffer it supplied.
//
// It is NOT pc_mnt_ram(): the RAM disk keeps no clock and documents mtime as always 0, and the
// conditional-GET tests (Last-Modified / If-Modified-Since) need a real timestamp per file.
//
// Two knobs model the failures a real store has and memory does not: a short-read cap, so the
// paging loop is driven across several reads, and a path whose open is forced to fail.
//
// Reads come from the registry; writes land in one capture buffer a test reads back, because what
// an upload asserts is the bytes that reached the store, not where they were filed.

#ifndef PROTOCORE_MNT_MOCK_H
#define PROTOCORE_MNT_MOCK_H

#include "server/filesystem/mnt.h"

#define MOCK_MNT_FILES 16
#define MOCK_MNT_HANDLES 8
#define MOCK_MNT_WRITE_CAP 8192

typedef struct
{
    const char *path;
    const uint8_t *data;
    size_t size;
    uint32_t mtime;
} MockMntEntry;

typedef struct
{
    proto_bool open;
    int entry; ///< index into files[], or -1 for the legacy single file
    size_t pos;
} MockMntHandle;

typedef struct
{
    MockMntEntry files[MOCK_MNT_FILES];
    int count;

    // Legacy single-file mode: when no path entry is registered, every open succeeds and yields
    // these bytes. Several tests only care about the body, not about which name produced it.
    const uint8_t *one_data;
    size_t one_size;
    proto_bool one_valid;

    size_t read_limit;          ///< cap on one read(), so a caller has to loop
    const char *open_fail_path; ///< opening exactly this path returns -1

    // What a writer sent, kept whole so a test can compare the bytes it streamed. The store is the
    // resource here, so the fixture records the write rather than the caller reporting it.
    uint8_t wbuf[MOCK_MNT_WRITE_CAP];
    size_t wlen;

    MockMntHandle h[MOCK_MNT_HANDLES];
} MockMntCtx;

// One instance for the whole program: the test registers files from its own translation unit and
// the code under test reads them from another. See the note on linkage in pc_net_host.h.
__attribute__((weak)) MockMntCtx g_mock_mnt;

static inline int mock_mnt_find(const char *path)
{
    for (int i = 0; i < g_mock_mnt.count; i++)
    {
        if (strcmp(g_mock_mnt.files[i].path, path) == 0)
        {
            return i;
        }
    }
    return -1;
}

static inline void mock_mnt_add(const char *path, const uint8_t *data, size_t size, uint32_t mtime)
{
    if (g_mock_mnt.count < MOCK_MNT_FILES)
    {
        g_mock_mnt.files[g_mock_mnt.count].path = path;
        g_mock_mnt.files[g_mock_mnt.count].data = data;
        g_mock_mnt.files[g_mock_mnt.count].size = size;
        g_mock_mnt.files[g_mock_mnt.count].mtime = mtime;
        g_mock_mnt.count++;
    }
}

static inline void mock_mnt_add_text(const char *path, const char *text, uint32_t mtime)
{
    mock_mnt_add(path, (const uint8_t *)text, strlen(text), mtime);
}

static inline void mock_mnt_set(const uint8_t *data, size_t size)
{
    g_mock_mnt.one_data = data;
    g_mock_mnt.one_size = size;
    g_mock_mnt.one_valid = PROTO_TRUE;
}

static inline void mock_mnt_set_text(const char *text)
{
    mock_mnt_set((const uint8_t *)text, strlen(text));
}

static inline void mock_mnt_clear(void)
{
    g_mock_mnt.one_data = NULL;
    g_mock_mnt.one_size = 0;
    g_mock_mnt.one_valid = PROTO_FALSE;
}

static inline void mock_mnt_reset(void)
{
    g_mock_mnt.count = 0;
    mock_mnt_clear();
    g_mock_mnt.read_limit = (size_t)-1;
    g_mock_mnt.open_fail_path = "";
    g_mock_mnt.wlen = 0;
    for (int i = 0; i < MOCK_MNT_HANDLES; i++)
    {
        g_mock_mnt.h[i].open = PROTO_FALSE;
    }
}

static inline void mock_mnt_read_limit(size_t n)
{
    g_mock_mnt.read_limit = n;
}

static inline void mock_mnt_fail_open(const char *path)
{
    g_mock_mnt.open_fail_path = path;
}

// --- the backend ------------------------------------------------------------

static inline int mock_mnt_open(const char *path, int mode)
{
    if (g_mock_mnt.open_fail_path && g_mock_mnt.open_fail_path[0] && strcmp(path, g_mock_mnt.open_fail_path) == 0)
    {
        return -1;
    }
    int e = mock_mnt_find(path);
    if (e < 0 && mode != (int)PC_MNT_READ)
    {
        e = -1; // a write creates: the bytes land in the write capture, not in the registry
    }
    else if (e < 0 && (g_mock_mnt.count > 0 || !g_mock_mnt.one_valid))
    {
        return -1; // path-aware once anything is registered; otherwise the legacy single file
    }
    for (int i = 0; i < MOCK_MNT_HANDLES; i++)
    {
        if (!g_mock_mnt.h[i].open)
        {
            g_mock_mnt.h[i].open = PROTO_TRUE;
            g_mock_mnt.h[i].entry = e;
            g_mock_mnt.h[i].pos = 0;
            return i;
        }
    }
    return -1;
}

static inline const uint8_t *mock_mnt_bytes(int entry, size_t *size)
{
    if (entry >= 0)
    {
        *size = g_mock_mnt.files[entry].size;
        return g_mock_mnt.files[entry].data;
    }
    *size = g_mock_mnt.one_size;
    return g_mock_mnt.one_data;
}

static inline int mock_mnt_read(int handle, void *buf, size_t n)
{
    if (handle < 0 || handle >= MOCK_MNT_HANDLES || !g_mock_mnt.h[handle].open)
    {
        return -1;
    }
    size_t size = 0;
    const uint8_t *src = mock_mnt_bytes(g_mock_mnt.h[handle].entry, &size);
    size_t pos = g_mock_mnt.h[handle].pos;
    size_t left = (pos < size) ? size - pos : 0;
    if (n > left)
    {
        n = left;
    }
    if (n > g_mock_mnt.read_limit)
    {
        n = g_mock_mnt.read_limit;
    }
    if (n > 0)
    {
        memcpy(buf, src + pos, n);
        g_mock_mnt.h[handle].pos = pos + n;
    }
    return (int)n;
}

static inline int mock_mnt_write(int handle, const void *buf, size_t n)
{
    if (handle < 0 || handle >= MOCK_MNT_HANDLES || !g_mock_mnt.h[handle].open)
    {
        return -1;
    }
    size_t room = sizeof(g_mock_mnt.wbuf) - g_mock_mnt.wlen;
    if (n > room)
    {
        n = room; // a full store short-writes; the caller sees the count it actually placed
    }
    memcpy(g_mock_mnt.wbuf + g_mock_mnt.wlen, buf, n);
    g_mock_mnt.wlen += n;
    return (int)n;
}

/** @brief Bytes a writer has sent since the last reset. */
static inline size_t mock_mnt_written(void)
{
    return g_mock_mnt.wlen;
}

/** @brief Those bytes. */
static inline const uint8_t *mock_mnt_wdata(void)
{
    return g_mock_mnt.wbuf;
}

static inline void mock_mnt_write_reset(void)
{
    g_mock_mnt.wlen = 0;
}

/** @brief Pre-fill the capture so only (cap - n) bytes of room are left, to drive a short write. */
static inline void mock_mnt_write_fill(size_t n)
{
    g_mock_mnt.wlen = (n < sizeof(g_mock_mnt.wbuf)) ? n : sizeof(g_mock_mnt.wbuf);
}

static inline void mock_mnt_close(int handle)
{
    if (handle >= 0 && handle < MOCK_MNT_HANDLES)
    {
        g_mock_mnt.h[handle].open = PROTO_FALSE;
    }
}

static inline proto_bool mock_mnt_seek(int handle, uint64_t off)
{
    if (handle < 0 || handle >= MOCK_MNT_HANDLES || !g_mock_mnt.h[handle].open)
    {
        return PROTO_FALSE;
    }
    g_mock_mnt.h[handle].pos = (size_t)off;
    return PROTO_TRUE;
}

static inline long mock_mnt_size(const char *path)
{
    int e = mock_mnt_find(path);
    if (e >= 0)
    {
        return (long)g_mock_mnt.files[e].size;
    }
    if (g_mock_mnt.count == 0 && g_mock_mnt.one_valid)
    {
        return (long)g_mock_mnt.one_size;
    }
    return -1;
}

static inline proto_bool mock_mnt_exists(const char *path)
{
    return mock_mnt_size(path) >= 0 ? PROTO_TRUE : PROTO_FALSE;
}

static inline proto_bool mock_mnt_stat(const char *path, pc_mnt_stat *out)
{
    int e = mock_mnt_find(path);
    if (e >= 0)
    {
        out->is_dir = PROTO_FALSE;
        out->size = g_mock_mnt.files[e].size;
        out->mtime = g_mock_mnt.files[e].mtime;
        return PROTO_TRUE;
    }
    if (g_mock_mnt.count == 0 && g_mock_mnt.one_valid)
    {
        out->is_dir = PROTO_FALSE;
        out->size = g_mock_mnt.one_size;
        out->mtime = 0;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static inline proto_bool mock_mnt_no_path(const char *path)
{
    (void)path;
    return PROTO_FALSE;
}

static inline proto_bool mock_mnt_no_rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    return PROTO_FALSE;
}

static inline int mock_mnt_opendir(const char *path)
{
    (void)path;
    return -1;
}

static inline proto_bool mock_mnt_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap)
{
    (void)handle;
    (void)out;
    (void)name;
    (void)name_cap;
    return PROTO_FALSE;
}

__attribute__((weak)) const pc_mnt_backend g_mock_mnt_backend = {
    mock_mnt_open,    mock_mnt_read,   mock_mnt_write,   mock_mnt_close,     mock_mnt_seek,
    mock_mnt_size,    mock_mnt_exists, mock_mnt_no_path, mock_mnt_no_rename, mock_mnt_no_path,
    mock_mnt_no_path, mock_mnt_stat,   mock_mnt_opendir, mock_mnt_readdir};

static inline const pc_mnt_backend *mock_mnt(void)
{
    return &g_mock_mnt_backend;
}

#endif // PROTOCORE_MNT_MOCK_H
