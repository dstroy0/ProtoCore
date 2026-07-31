// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file docstore.cpp
 * @brief Local JSON document store on the WAL: dbm + top-level JSON field queries (see docstore.h).
 */

#include "services/storage/docstore/docstore.h"

#if PC_ENABLE_DOCSTORE

#include "network_drivers/presentation/codec/json/json.h"
#include <string.h>

void pc_docstore_open(pc_doc_store *ds, pc_dbm *db)
{
    ds->db = db;
}

bool pc_docstore_put(pc_doc_store *ds, const char *id, uint16_t id_len, const uint8_t *json, uint32_t json_len)
{
    return pc_dbm_put(ds->db, id, id_len, json, json_len);
}

long pc_docstore_get(pc_doc_store *ds, const char *id, uint16_t id_len, uint8_t *buf, size_t cap)
{
    return pc_dbm_get(ds->db, id, id_len, buf, cap);
}

bool pc_docstore_del(pc_doc_store *ds, const char *id, uint16_t id_len)
{
    return pc_dbm_del(ds->db, id, id_len);
}

bool pc_docstore_contains(const pc_doc_store *ds, const char *id, uint16_t id_len)
{
    return pc_dbm_contains(ds->db, id, id_len);
}

uint32_t pc_docstore_count(const pc_doc_store *ds)
{
    return pc_dbm_count(ds->db);
}

bool pc_docstore_sync(pc_doc_store *ds)
{
    return pc_dbm_sync(ds->db);
}

namespace
{
enum class FindKind : uint8_t
{
    FIND_STR,
    FIND_INT,
    FIND_BOOL
};

// Per-scan state carried through pc_dbm_iterate. `doc` reads each document's JSON body (NUL-terminated
// for the reader); `fieldtmp` extracts a string field for comparison. Both are bounded (no heap).
struct FindCtx
{
    pc_dbm *db;
    const char *field;
    FindKind kind;
    const char *sval;
    long ival;
    bool bval;
    pc_doc_match_cb user_cb;
    void *user_ctx;
    uint32_t matches;
    uint8_t doc[PC_DBM_VAL_MAX + 1];
    char fieldtmp[PC_DOCSTORE_FIELD_MAX + 1];
};

bool find_cb(const char *key, uint16_t key_len, void *vctx)
{
    FindCtx *f = (FindCtx *)vctx;
    long n = pc_dbm_get(f->db, key, key_len, f->doc, PC_DBM_VAL_MAX);
    if (n < 0)
    {
        return true; // unreadable (shouldn't happen mid-iteration) - skip
    }
    f->doc[n] = 0; // the JSON reader wants a NUL-terminated body
    const char *json = (const char *)f->doc;

    bool match = false;
    if (f->kind == FindKind::FIND_STR)
    {
        if (json_get_str(json, f->field, f->fieldtmp, sizeof(f->fieldtmp)))
        {
            match = (strcmp(f->fieldtmp, f->sval) == 0);
        }
    }
    else if (f->kind == FindKind::FIND_INT)
    {
        long v = 0;
        if (json_get_int(json, f->field, &v))
        {
            match = (v == f->ival);
        }
    }
    else
    {
        bool b = false;
        if (json_get_bool(json, f->field, &b))
        {
            match = (b == f->bval);
        }
    }

    if (match)
    {
        f->matches++;
        if (f->user_cb && !f->user_cb(key, key_len, f->doc, (uint32_t)n, f->user_ctx))
        {
            return false; // caller asked to stop
        }
    }
    return true;
}

uint32_t run_find(FindCtx *f)
{
    f->matches = 0;
    pc_dbm_iterate(f->db, find_cb, f);
    return f->matches;
}
} // namespace

uint32_t pc_docstore_find_str(pc_doc_store *ds, const char *field, const char *value, pc_doc_match_cb cb, void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FindKind::FIND_STR;
    f.sval = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

uint32_t pc_docstore_find_int(pc_doc_store *ds, const char *field, long value, pc_doc_match_cb cb, void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FindKind::FIND_INT;
    f.ival = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

uint32_t pc_docstore_find_bool(pc_doc_store *ds, const char *field, bool value, pc_doc_match_cb cb, void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FindKind::FIND_BOOL;
    f.bval = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

#endif // PC_ENABLE_DOCSTORE
