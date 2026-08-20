// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file docstore.c
 * @brief Local JSON document store on the WAL: dbm + top-level JSON field queries (see docstore.h).
 */

#include "services/storage/docstore/docstore.h"

#if PROTOCORE_ENABLE_DOCSTORE

#include "mmgr/protostr/protostr.h" // str.eq: the string-field find compare
#include "network_drivers/presentation/codec/json/json.h"

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

void protocore_docstore_open(protocore_doc_store *ds, struct protocore_dbm *db)
{
    ds->db = db;
}

proto_bool protocore_docstore_put(protocore_doc_store *ds, const char *id, uint16_t id_len, const uint8_t *json,
                                  uint32_t json_len)
{
    return protocore_dbm_put(ds->db, id, id_len, json, json_len);
}

long protocore_docstore_get(protocore_doc_store *ds, const char *id, uint16_t id_len, uint8_t *buf, size_t cap)
{
    return protocore_dbm_get(ds->db, id, id_len, buf, cap);
}

proto_bool protocore_docstore_del(protocore_doc_store *ds, const char *id, uint16_t id_len)
{
    return protocore_dbm_del(ds->db, id, id_len);
}

proto_bool protocore_docstore_contains(const protocore_doc_store *ds, const char *id, uint16_t id_len)
{
    return protocore_dbm_contains(ds->db, id, id_len);
}

uint32_t protocore_docstore_count(const protocore_doc_store *ds)
{
    return protocore_dbm_count(ds->db);
}

proto_bool protocore_docstore_sync(protocore_doc_store *ds)
{
    return protocore_dbm_sync(ds->db);
}

typedef enum PROTO_ENUM_PACKED
{
    FIND_STR,
    FIND_INT,
    FIND_BOOL
} FindKind;

// Per-scan state carried through protocore_dbm_iterate. `doc` reads each document's JSON body (NUL-terminated
// for the reader); `fieldtmp` extracts a string field for comparison. Both are bounded (no heap).
typedef struct
{
    struct protocore_dbm *db;
    const char *field;
    FindKind kind;
    const char *sval;
    long ival;
    proto_bool bval;
    protocore_doc_match_cb user_cb;
    void *user_ctx;
    uint32_t matches;
    uint8_t doc[PROTOCORE_DBM_VAL_MAX + 1];
    char fieldtmp[PROTOCORE_DOCSTORE_FIELD_MAX + 1];
} FindCtx;

static proto_bool find_cb(const char *key, uint16_t key_len, void *vctx)
{
    FindCtx *f = (FindCtx *)vctx;
    long n = protocore_dbm_get(f->db, key, key_len, f->doc, PROTOCORE_DBM_VAL_MAX);
    if (n < 0)
    {
        return PROTO_TRUE; // unreadable (shouldn't happen mid-iteration) - skip
    }
    f->doc[n] = 0; // the JSON reader wants a NUL-terminated body
    const char *json = (const char *)f->doc;

    proto_bool match = PROTO_FALSE;
    if (f->kind == FIND_STR)
    {
        Json.get_str_args.json = json;
        Json.get_str_args.key = f->field;
        Json.get_str_args.out = f->fieldtmp;
        Json.get_str_args.out_cap = sizeof(f->fieldtmp);
        Json.get_str(json_work);
        if (Json.ok)
        {
            match = str.eq(f->fieldtmp, f->sval, sizeof(f->fieldtmp), PROTO_FALSE);
        }
    }
    else if (f->kind == FIND_INT)
    {
        long v = 0;
        Json.get_int_args.json = json;
        Json.get_int_args.key = f->field;
        Json.get_int_args.out = &v;
        Json.get_int(json_work);
        if (Json.ok)
        {
            match = (v == f->ival);
        }
    }
    else
    {
        proto_bool b = PROTO_FALSE;
        Json.get_bool_args.json = json;
        Json.get_bool_args.key = f->field;
        Json.get_bool_args.out = &b;
        Json.get_bool(json_work);
        if (Json.ok)
        {
            match = (b == f->bval);
        }
    }

    if (match)
    {
        f->matches++;
        if (f->user_cb && !f->user_cb(key, key_len, f->doc, (uint32_t)n, f->user_ctx))
        {
            return PROTO_FALSE; // caller asked to stop
        }
    }
    return PROTO_TRUE;
}

static uint32_t run_find(FindCtx *f)
{
    f->matches = 0;
    protocore_dbm_iterate(f->db, find_cb, f);
    return f->matches;
}

uint32_t protocore_docstore_find_str(protocore_doc_store *ds, const char *field, const char *value,
                                     protocore_doc_match_cb cb, void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FIND_STR;
    f.sval = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

uint32_t protocore_docstore_find_int(protocore_doc_store *ds, const char *field, long value, protocore_doc_match_cb cb,
                                     void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FIND_INT;
    f.ival = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

uint32_t protocore_docstore_find_bool(protocore_doc_store *ds, const char *field, proto_bool value,
                                      protocore_doc_match_cb cb, void *ctx)
{
    FindCtx f;
    f.db = ds->db;
    f.field = field;
    f.kind = FIND_BOOL;
    f.bval = value;
    f.user_cb = cb;
    f.user_ctx = ctx;
    return run_find(&f);
}

#endif // PROTOCORE_ENABLE_DOCSTORE
