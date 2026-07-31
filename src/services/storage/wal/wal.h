// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wal.h
 * @brief Write-ahead journal for atomic buffer-to-flash storage (PC_ENABLE_WAL).
 *
 * A power-loss-safe write-ahead log, the substrate for on-device data stores (dbm / sqlite / nosql). It
 * is built to the envelope measured on real hardware (docs/FEATURE_PERFORMANCE.md): an SD card over SPI
 * writes ~1.5 MB/s sequentially but only ~40-100 durable random IOPS with 100+ ms tail spikes, and the
 * durable-throughput knee is a ~32 KiB write with a flush every ~128-256 KiB. So the log **appends
 * sequentially** in fixed pages and checkpoints in bulk, never scattering small durable writes.
 *
 * **On-media record framing (this file - increment 1, pure + host-tested):** each record is
 *
 *     [magic u32][seq u64][len u32][crc u32][payload len bytes]   (little-endian; ::WAL_RECORD_HEADER = 20)
 *
 * where `crc` is a CRC-32 (IEEE 802.3) over the 16 header bytes plus the payload. Recovery on mount walks
 * records from the start and **stops at the first record with a bad magic, a bad CRC, or a truncated tail**
 * - i.e. the torn write left by a power loss - so a crash costs at most the last, un-checkpointed record.
 * This is the atomicity core, and it is pure (no I/O) so it is fully host-testable: feed it a journal
 * image with a corrupted or truncated tail and it recovers to the last good record.
 *
 * The durable store layer - A/B superblock, checkpoint, and mount/recover over a block-device seam - is
 * built on this codec in pc_wal_store.h; pc_wal_fs.h binds that seam to a real fs::FS file (SD / LittleFS). The
 * whole path is hardware-verified on an SD card over SPI (checkpoint recovery, torn-tail drop, byte-level
 * payload persistence, and survival across a chip reset all pass).
 */

#ifndef PROTOCORE_WAL_H
#define PROTOCORE_WAL_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_WAL

/** @brief Bytes of fixed record header before the payload (magic + seq + len + crc). */
#define WAL_RECORD_HEADER 20

/** @brief Record magic ("WAL1", little-endian) - the resync marker recovery looks for. */
#define WAL_MAGIC 0x314C4157u

/** @brief CRC-32 (IEEE 802.3, poly 0xEDB88320, init/final 0xFFFFFFFF) over @p data. */
uint32_t pc_wal_crc32(const uint8_t *data, size_t len);

/**
 * @name Streaming CRC-32
 * The same CRC as ::pc_wal_crc32, split so a record header and a large payload can be folded in without
 * ever buffering both together - which is how the store CRCs an append (header then payload) and how
 * recovery CRCs a record it reads back from media in small chunks.
 * @{
 */
uint32_t pc_wal_crc32_init(void);                                       ///< seed (0xFFFFFFFF)
uint32_t pc_wal_crc32_update(uint32_t crc, const uint8_t *d, size_t n); ///< fold @p n bytes into @p crc
uint32_t pc_wal_crc32_final(uint32_t crc);                              ///< finalize (xor 0xFFFFFFFF)
/** @} */

/**
 * @brief Encode one journal record into @p out.
 * @param seq     the record's monotonic sequence number.
 * @param payload the record body (may be null when @p len is 0).
 * @return total bytes written (::WAL_RECORD_HEADER + @p len), or 0 if it does not fit @p cap.
 */
size_t pc_wal_record_encode(uint8_t *out, size_t cap, uint64_t seq, const uint8_t *payload, uint32_t len);

/** @brief Per-record callback for ::pc_wal_replay. */
using WalRecordCb = void (*)(uint64_t seq, const uint8_t *payload, uint32_t len, void *ctx);

/**
 * @brief Replay a journal image, invoking @p cb for each valid record in order.
 *
 * Stops at the first record with a bad magic, a failed CRC, or a length that runs past @p len (a
 * truncated tail from a power loss). @return the offset just past the last good record - the durable
 * journal length; any bytes beyond it are the torn tail to discard/overwrite.
 */
size_t pc_wal_replay(const uint8_t *img, size_t len, WalRecordCb cb, void *ctx);

#endif // PC_ENABLE_WAL
#endif // PROTOCORE_WAL_H
