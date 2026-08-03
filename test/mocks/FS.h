#pragma once
// Mock filesystem for native unit tests.
// Mimics the subset of the ESP32 Arduino fs::FS / fs::File API used by
// PC::serve_file().

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace fs
{

// Test hook: FS::open() of exactly this path returns an invalid File even when the node
// exists - models a resource that exists()/stat reports but that cannot be opened (a
// concurrent delete, permission, or FS error), exercising the TOCTOU re-open guards.
// Empty (default) disables it. Declared early so the tree/reset helpers can clear it.
inline const char *&_mock_open_fail_path()
{
    static const char *p = "";
    return p;
}

// ---------------------------------------------------------------------------
// Opt-in in-memory directory tree (for WebDAV / recursive-FS handler tests)
//
// Disabled by default: with the tree off, every function below behaves exactly
// as the original files-only mock, so existing tests are completely unaffected.
// A test that needs directories (mkdir / openNextFile / isDirectory / rename /
// rmdir) calls mock_fs_tree_enable() in setUp(); open("w"/"a") then persists per
// path so a recursive copy can read written files back.
// ---------------------------------------------------------------------------

struct MockNode
{
    char path[160];
    bool used;
    bool is_dir;
    size_t len;
    uint8_t data[2048];
};

inline MockNode *_tree_nodes()
{
    static MockNode n[64];
    return n;
}
inline bool &_tree_on()
{
    static bool on = false;
    return on;
}
inline MockNode *_tree_find(const char *path)
{
    MockNode *n = _tree_nodes();
    for (int i = 0; i < 64; i++)
    {
        if (n[i].used && strcmp(n[i].path, path) == 0)
        {
            return &n[i];
        }
    }
    return nullptr;
}
inline MockNode *_tree_add(const char *path, bool is_dir)
{
    MockNode *e = _tree_find(path);
    if (e)
    {
        return e;
    }
    MockNode *n = _tree_nodes();
    for (int i = 0; i < 64; i++)
    {
        if (!n[i].used)
        {
            n[i].used = true;
            n[i].is_dir = is_dir;
            n[i].len = 0;
            snprintf(n[i].path, sizeof(n[i].path), "%s", path);
            return &n[i];
        }
    }
    return nullptr;
}
// True when `path` is a direct child of directory `dir` (exactly one level deeper).
inline bool _tree_is_child(const char *dir, const char *path)
{
    size_t dl = strlen(dir);
    if (strncmp(path, dir, dl) != 0 || path[dl] != '/')
    {
        return false;
    }
    return strchr(path + dl + 1, '/') == nullptr; // no further separator -> direct child
}
inline void mock_fs_tree_enable()
{
    _tree_on() = true;
    _mock_open_fail_path() = ""; // clear a forced open-failure a prior test may have set
    MockNode *n = _tree_nodes();
    for (int i = 0; i < 64; i++)
    {
        n[i].used = false;
    }
}
inline void mock_fs_tree_disable()
{
    _tree_on() = false;
}

// ---------------------------------------------------------------------------
// Shared mock state -- use mock_fs_set() / mock_fs_clear() in tests
// ---------------------------------------------------------------------------

inline const uint8_t *&_mock_data()
{
    static const uint8_t *p = nullptr;
    return p;
}
inline size_t &_mock_size()
{
    static size_t s = 0;
    return s;
}
inline bool &_mock_valid()
{
    static bool v = false;
    return v;
}

inline void mock_fs_set(const uint8_t *data, size_t size)
{
    _mock_data() = data;
    _mock_size() = size;
    _mock_valid() = true;
}

inline void mock_fs_set(const char *text)
{
    _mock_data() = (const uint8_t *)(text);
    _mock_size() = strlen(text);
    _mock_valid() = true;
}

inline void mock_fs_clear()
{
    _mock_data() = nullptr;
    _mock_size() = 0;
    _mock_valid() = false;
}

// Test hook: cap the total bytes File::read() will yield, regardless of file size -
// models a truncated file / mid-body I/O error so the read returns 0 while bytes
// remain. SIZE_MAX (default) reads normally.
inline size_t &_mock_read_limit()
{
    static size_t v = (size_t)-1;
    return v;
}

// ---------------------------------------------------------------------------
// Write capture (for streaming-upload tests): File::write() appends here when a
// file is opened in "w"/"a" mode.
// ---------------------------------------------------------------------------

inline uint8_t *_mock_wbuf()
{
    static uint8_t b[8192];
    return b;
}
inline size_t &_mock_wlen()
{
    static size_t n = 0;
    return n;
}
inline void mock_fs_write_reset()
{
    _mock_wlen() = 0;
}
inline size_t mock_fs_written()
{
    return _mock_wlen();
}
inline const uint8_t *mock_fs_wdata()
{
    return _mock_wbuf();
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

class File
{
    const uint8_t *_data;
    size_t _size;
    size_t _pos;
    bool _open;
    time_t _mtime;
    // Tree-mode extensions (all null/false in legacy mode).
    MockNode *_wnode; ///< write target node (tree open "w"/"a")
    bool _is_dir;     ///< this File is a directory
    char _dir[160];   ///< directory path, for openNextFile iteration
    int _cursor;      ///< next node index to scan for openNextFile

  public:
    File() : _data(nullptr), _size(0), _pos(0), _open(false), _mtime(0), _wnode(nullptr), _is_dir(false), _cursor(0)
    {
        _dir[0] = '\0';
    }
    File(const uint8_t *data, size_t size, time_t mtime = 0)
        : _data(data), _size(size), _pos(0), _open(true), _mtime(mtime), _wnode(nullptr), _is_dir(false), _cursor(0)
    {
        _dir[0] = '\0';
    }
    // Writable sink: bytes go to the shared write-capture buffer.
    explicit File(bool open_writable)
        : _data(nullptr), _size(0), _pos(0), _open(open_writable), _mtime(0), _wnode(nullptr), _is_dir(false),
          _cursor(0)
    {
        _dir[0] = '\0';
    }
    // Tree-backed file: read from / write to an in-memory node.
    File(MockNode *node, bool writing)
        : _data(node ? node->data : nullptr), _size(node ? node->len : 0), _pos(0), _open(node != nullptr), _mtime(0),
          _wnode(writing ? node : nullptr), _is_dir(node ? node->is_dir : false), _cursor(0)
    {
        snprintf(_dir, sizeof(_dir), "%s", node ? node->path : "");
    }

    size_t write(const uint8_t *src, size_t sz)
    {
        if (!_open)
        {
            return 0;
        }
        if (_wnode) // tree mode: append into the node (bounded by its buffer)
        {
            size_t cap = sizeof(_wnode->data) - _wnode->len;
            size_t w = sz < cap ? sz : cap;
            memcpy(_wnode->data + _wnode->len, src, w);
            _wnode->len += w;
            return w;
        }
        size_t cap = 8192 - _mock_wlen();
        size_t w = sz < cap ? sz : cap;
        memcpy(_mock_wbuf() + _mock_wlen(), src, w);
        _mock_wlen() += w;
        return w;
    }
    size_t write(uint8_t b)
    {
        return write(&b, 1);
    }

    size_t read(uint8_t *dst, size_t sz)
    {
        if (!_open || _pos >= _size)
        {
            return 0;
        }
        size_t cap = _mock_read_limit();
        if (_pos >= cap)
        {
            return 0; // truncated/short-reading file: 0 with bytes still remaining
        }
        size_t avail = _size - _pos;
        size_t n = (sz < avail) ? sz : avail;
        if (_pos + n > cap)
        {
            n = cap - _pos; // clamp this read to the truncation point
        }
        memcpy(dst, _data + _pos, n);
        _pos += n;
        return n;
    }

    bool isDirectory() const
    {
        return _is_dir;
    }
    const char *name() const
    {
        return _dir; // full path; the server applies dav_basename() to it
    }
    // Iterate direct children of this directory (tree mode). Returns an invalid
    // File when exhausted, matching fs::File::openNextFile().
    File openNextFile(const char * = "r")
    {
        if (!_is_dir)
        {
            return File();
        }
        MockNode *nodes = _tree_nodes();
        for (; _cursor < 64; _cursor++)
        {
            MockNode *c = &nodes[_cursor];
            if (c->used && _tree_is_child(_dir, c->path))
            {
                _cursor++;
                return File(c, false);
            }
        }
        return File();
    }

    // Mirrors fs::File::seek(pos, SeekSet) for Range request tests.
    bool seek(uint32_t pos)
    {
        if (!_open || pos > _size)
        {
            return false;
        }
        _pos = pos;
        return true;
    }

    size_t size() const
    {
        return _size;
    }
    time_t getLastWrite() const
    {
        return _mtime;
    }
    void close()
    {
        _open = false;
    }
    explicit operator bool() const
    {
        return _open;
    }
};

// ---------------------------------------------------------------------------
// Path-aware registry (for serve_static / gzip tests)
//
// Legacy single-file API (mock_fs_set / open path-agnostic) is preserved for
// existing tests. When at least one mock_fs_add(path,...) entry is registered,
// open()/exists() become path-aware and the legacy single file is ignored.
// ---------------------------------------------------------------------------

struct MockFsEntry
{
    const char *path;
    const uint8_t *data;
    size_t size;
    time_t mtime;
};

inline MockFsEntry *_mock_entries()
{
    static MockFsEntry e[16];
    return e;
}
inline int &_mock_entry_count()
{
    static int n = 0;
    return n;
}

inline void mock_fs_add(const char *path, const uint8_t *data, size_t size, time_t mtime = 0)
{
    int &n = _mock_entry_count();
    if (n < 16)
    {
        _mock_entries()[n].path = path;
        _mock_entries()[n].data = data;
        _mock_entries()[n].size = size;
        _mock_entries()[n].mtime = mtime;
        n++;
    }
}
inline void mock_fs_add(const char *path, const char *text, time_t mtime = 0)
{
    mock_fs_add(path, (const uint8_t *)(text), strlen(text), mtime);
}
inline void mock_fs_reset()
{
    _mock_entry_count() = 0;
    mock_fs_clear();
    _mock_read_limit() = (size_t)-1; // clear a short-read cap a prior test may have set
    _mock_open_fail_path() = "";     // clear a forced open-failure a prior test may have set
}
inline const MockFsEntry *_mock_find(const char *path)
{
    for (int i = 0; i < _mock_entry_count(); i++)
    {
        if (strcmp(_mock_entries()[i].path, path) == 0)
        {
            return &_mock_entries()[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// FS
// ---------------------------------------------------------------------------

class FS
{
  public:
    File open(const char *path, const char *mode = "r")
    {
        if (_mock_open_fail_path()[0] && strcmp(path, _mock_open_fail_path()) == 0)
        {
            return File(); // test hook: exists() but open() fails (TOCTOU / FS error)
        }
        if (_tree_on())
        {
            bool writing = mode && (mode[0] == 'w' || mode[0] == 'a');
            if (writing)
            {
                MockNode *n = _tree_find(path);
                if (!n)
                {
                    n = _tree_add(path, false);
                }
                if (n && mode[0] == 'w')
                {
                    n->len = 0; // truncate
                }
                return File(n, true);
            }
            MockNode *n = _tree_find(path);
            return n ? File(n, false) : File();
        }
        // Write/append mode -> a writable sink (captured via mock_fs_written()).
        if (mode && (mode[0] == 'w' || mode[0] == 'a'))
        {
            return File(true);
        }
        if (_mock_entry_count() > 0)
        {
            const MockFsEntry *e = _mock_find(path);
            return e ? File(e->data, e->size, e->mtime) : File();
        }
        // Legacy path-agnostic single-file behavior.
        if (_mock_valid() && _mock_data())
        {
            return File(_mock_data(), _mock_size());
        }
        return File(); // invalid - signals file-not-found
    }

    bool exists(const char *path)
    {
        if (_tree_on())
        {
            return _tree_find(path) != nullptr;
        }
        if (_mock_entry_count() > 0)
        {
            return _mock_find(path) != nullptr;
        }
        return _mock_valid(); // legacy: any path "exists" if a file is set
    }

    // Directory operations - only meaningful in tree mode; no-ops/false otherwise.
    bool mkdir(const char *path)
    {
        if (!_tree_on())
        {
            return true; // legacy callers don't model dirs
        }
        if (_tree_find(path))
        {
            return false; // already exists
        }
        return _tree_add(path, true) != nullptr;
    }
    bool rmdir(const char *path)
    {
        return remove(path);
    }
    bool remove(const char *path)
    {
        if (!_tree_on())
        {
            return true;
        }
        MockNode *n = _tree_find(path);
        if (!n)
        {
            return false;
        }
        n->used = false;
        return true;
    }
    bool rename(const char *from, const char *to)
    {
        if (!_tree_on())
        {
            return true;
        }
        MockNode *n = _tree_find(from);
        if (!n)
        {
            return false;
        }
        // Re-path the node and (for a directory) every descendant under it.
        size_t fl = strlen(from);
        MockNode *nodes = _tree_nodes();
        for (int i = 0; i < 64; i++)
        {
            MockNode *c = &nodes[i];
            if (!c->used)
            {
                continue;
            }
            if (strcmp(c->path, from) == 0 || (strncmp(c->path, from, fl) == 0 && c->path[fl] == '/'))
            {
                char np[160];
                snprintf(np, sizeof(np), "%s%s", to, c->path + fl);
                snprintf(c->path, sizeof(c->path), "%s", np);
            }
        }
        return true;
    }
};

} // namespace fs
