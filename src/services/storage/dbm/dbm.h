// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dbm.h
 * @brief Log-structured hash key-value store on the WAL (PC_ENABLE_DBM, requires PC_ENABLE_WAL).
 *
 * A Bitcask-style key-value store: the value data lives append-only in the write-ahead log (pc_wal_store.h)
 * and an in-RAM open-addressed hash index maps each live key to where its latest value sits in the log.
 * This is the design the measured SD envelope wants (docs/FEATURE_PERFORMANCE.md): every write is one of
 * the WAL's fast sequential appends, never a slow durable random write.
 *
 *  - **put / delete** append one record to the WAL and update the index. Writes are batched (unsynced);
 *    call ::pc_dbm_sync to checkpoint the WAL and make them durable.
 *  - **get** looks up the index and re-reads the value straight from the log (no per-key RAM copy).
 *  - **open** rebuilds the index by scanning the WAL, replaying puts and deletes in order, so the live
 *    key set is exactly what survived the last mount of the underlying store.
 *
 * The index is a fixed BSS array of ::PC_DBM_SLOTS slots (no heap); keys are bounded by
 * ::PC_DBM_KEY_MAX and values by ::PC_DBM_VAL_MAX. Everything fails closed at those bounds. Like the
 * other services, drive it from one context (a worker / loop), not concurrently.
 *
 * On-media record payload (inside a WAL record): `[op u8][key_len u16][val_len u32][key][value]` (LE),
 * op 0 = put, op 1 = delete (a tombstone, val_len 0).
 */

#ifndef PROTOCORE_DBM_H
#define PROTOCORE_DBM_H

#include "protocore_config.h"

#if PC_ENABLE_DBM

#include "services/storage/wal/wal_store.h"
#include <stddef.h>
#include <stdint.h>

/** @brief One in-RAM index slot. `state`: 0 empty, 1 live, 2 deleted (tombstone, still probed through). */
struct pc_dbm_slot
{
    uint8_t state;
    uint16_t key_len;
    uint64_t hash;
    uint64_t val_off; ///< data-region offset of the value bytes in the WAL
    uint32_t val_len;
    char key[PC_DBM_KEY_MAX];
};

/** @brief A dbm handle bound to a mounted ::WalStore. Declare one (static for BSS); no heap. */
struct pc_dbm
{
    WalStore *wal;
    uint32_t count; ///< live keys
    pc_dbm_slot slots[PC_DBM_SLOTS];
};

/**
 * @brief Bind @p db to a mounted @p wal and rebuild the index by replaying the log.
 * @return false if the log holds more distinct live keys than ::PC_DBM_SLOTS (index would overflow).
 */
bool pc_dbm_open(pc_dbm *db, WalStore *wal);

/**
 * @brief Insert or overwrite @p key -> @p val. Appends a WAL record and updates the index (not synced).
 * @return false if @p key_len > ::PC_DBM_KEY_MAX, @p val_len > ::PC_DBM_VAL_MAX, the index is full
 * (a new key with no free slot), or the WAL is full.
 */
bool pc_dbm_put(pc_dbm *db, const char *key, uint16_t key_len, const uint8_t *val, uint32_t val_len);

/**
 * @brief Fetch @p key's value into @p buf (up to @p cap).
 * @return the value length on success, or -1 if the key is absent or the value is larger than @p cap.
 */
long pc_dbm_get(pc_dbm *db, const char *key, uint16_t key_len, uint8_t *buf, size_t cap);

/**
 * @brief Delete @p key (appends a tombstone record and drops it from the index).
 * @return true if the key existed (and the tombstone was appended); false if absent or the WAL is full.
 */
bool pc_dbm_del(pc_dbm *db, const char *key, uint16_t key_len);

/** @brief @return true if @p key is live. */
bool pc_dbm_contains(const pc_dbm *db, const char *key, uint16_t key_len);

/** @brief @return the number of live keys. */
uint32_t pc_dbm_count(const pc_dbm *db);

/** @brief Make all writes since the last sync durable (checkpoints the WAL). @return false on I/O failure. */
bool pc_dbm_sync(pc_dbm *db);

/** @brief Per-key callback for ::pc_dbm_iterate; return false to stop early. The key bytes are not
 * NUL-terminated. Do not put/delete during iteration (it mutates the index). */
using pc_dbm_iter_cb = bool (*)(const char *key, uint16_t key_len, void *ctx);

/** @brief Visit every live key (unordered). @return the number of keys visited. */
uint32_t pc_dbm_iterate(const pc_dbm *db, pc_dbm_iter_cb cb, void *ctx);

/**
 * @brief Bytes the live keys would occupy after a compaction (the summed framed size of one WAL record per
 * live key). The current log (::pc_wal_store_used on the bound store) is always at least this large; the
 * difference is reclaimable dead space from overwritten and deleted keys. Pair the two to decide when the
 * dead fraction is worth a ::pc_dbm_compact.
 */
uint64_t pc_dbm_live_bytes(const pc_dbm *db);

/**
 * @brief Compact the store: copy only the live keys (the latest value each, no tombstones) into a freshly
 * formatted destination @p dst, checkpoint it, then rebind @p db to @p dst and rebuild the index - reclaiming
 * all space held by overwritten / deleted keys (Bitcask-style merge to new, never in place).
 *
 * @p dst must be a mounted, freshly formatted ::WalStore backed by a DIFFERENT device than @p db's current
 * log. On success @p db reads and writes through @p dst (the old device can then be reused); on failure
 * (@p dst too small, or an I/O error) @p db is left UNCHANGED on its original log, so no data is lost and the
 * caller can retry or keep using the old store.
 * @return true when every live key was copied, the destination checkpointed, and the index rebuilt.
 */
bool pc_dbm_compact(pc_dbm *db, WalStore *dst);

#endif // PC_ENABLE_DBM
#endif // PROTOCORE_DBM_H
