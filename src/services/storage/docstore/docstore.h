// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file docstore.h
 * @brief Local JSON document store on the WAL (PROTOCORE_ENABLE_DOCSTORE, requires DBM + WAL).
 *
 * A small NoSQL document store: documents are JSON objects addressed by an id, kept durably on the
 * write-ahead log. It is a thin layer over dbm (dbm.h) - the id is the key, the JSON body is the value -
 * so it inherits dbm's zero-heap index, WAL persistence, and crash recovery for free. What it adds on top
 * is the document capability: **field queries** - scan the live documents and match those whose top-level
 * JSON field equals a value (like a small `find({field: value})`), using the zero-heap JSON reader
 * (json.h). Values are bounded by ::PROTOCORE_DBM_VAL_MAX, ids by ::PROTOCORE_DBM_KEY_MAX.
 *
 * Writes are batched; call ::protocore_docstore_sync to checkpoint the WAL and make them durable. Drive it from
 * one context (a worker / loop), not concurrently, and do not put/delete from inside a find callback.
 */

#ifndef PROTOCORE_DOCSTORE_H
#define PROTOCORE_DOCSTORE_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_DOCSTORE

#include "services/storage/dbm/dbm.h"

/** @brief A document store bound to a mounted ::protocore_dbm. */
typedef struct
{
    struct protocore_dbm *db;
} protocore_doc_store;

/** @brief Bind @p ds to an open @p db. */
void protocore_docstore_open(protocore_doc_store *ds, struct protocore_dbm *db);

/**
 * @brief Insert or replace the document @p id with JSON body @p json. Not synced (batched).
 * @return false on the same bounds/full conditions as ::protocore_dbm_put.
 */
proto_bool protocore_docstore_put(protocore_doc_store *ds, const char *id, uint16_t id_len, const uint8_t *json,
                                  uint32_t json_len);

/**
 * @brief Fetch document @p id's JSON body into @p buf (up to @p cap).
 * @return the body length, or -1 if absent or larger than @p cap.
 */
long protocore_docstore_get(protocore_doc_store *ds, const char *id, uint16_t id_len, uint8_t *buf, size_t cap);

/** @brief Delete document @p id. @return true if it existed. */
proto_bool protocore_docstore_del(protocore_doc_store *ds, const char *id, uint16_t id_len);

/** @brief @return true if document @p id exists. */
proto_bool protocore_docstore_contains(const protocore_doc_store *ds, const char *id, uint16_t id_len);

/** @brief @return the number of documents. */
uint32_t protocore_docstore_count(const protocore_doc_store *ds);

/** @brief Make all writes durable (checkpoints the WAL). @return false on I/O failure. */
proto_bool protocore_docstore_sync(protocore_doc_store *ds);

/**
 * @brief Per-match callback for the find calls: the matching document's id and JSON body (the body points
 * into a temporary buffer valid only for this call). Return false to stop the scan early.
 */
typedef proto_bool (*protocore_doc_match_cb)(const char *id, uint16_t id_len, const uint8_t *json, uint32_t json_len,
                                             void *ctx);

/**
 * @brief Find documents whose top-level string field @p field equals @p value. @return the match count.
 * Field string values longer than ::PROTOCORE_DOCSTORE_FIELD_MAX will not match.
 */
uint32_t protocore_docstore_find_str(protocore_doc_store *ds, const char *field, const char *value,
                                     protocore_doc_match_cb cb, void *ctx);

/** @brief Find documents whose top-level integer field @p field equals @p value. @return the match count. */
uint32_t protocore_docstore_find_int(protocore_doc_store *ds, const char *field, long value, protocore_doc_match_cb cb,
                                     void *ctx);

/** @brief Find documents whose top-level boolean field @p field equals @p value. @return the match count. */
uint32_t protocore_docstore_find_bool(protocore_doc_store *ds, const char *field, proto_bool value,
                                      protocore_doc_match_cb cb, void *ctx);

#endif // PROTOCORE_ENABLE_DOCSTORE
#endif // PROTOCORE_DOCSTORE_H
