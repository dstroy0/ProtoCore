// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iolink.h
 * @brief IO-Link (SDCI, IEC 61131-9) data-link message codec (PC_ENABLE_IOLINK).
 *
 * IO-Link is the point-to-point 3-wire serial link to smart sensors / actuators. This codec
 * implements the data-link **message layer**: the M-sequence Control octet (MC), the
 * checksum / M-sequence-type octet (CKT) of a master message, the checksum / status octet
 * (CKS) of a device reply, and the SDCI message checksum that protects both directions.
 *
 * The checksum is the part everyone gets wrong, so it is implemented straight from the spec
 * (IO-Link Interface and System Specification v1.1.4, Annex A.1.6): a 0x52 seed XORed octet
 * by octet across the message (the check octet included with its checksum bits 0), then the
 * 8-to-6-bit compression of equation (A.1). `pc_iol_finalize` writes it into the check octet and
 * `pc_iol_verify` checks it.
 *
 * Scope: the message / DL layer. The per-type M-sequence octet layout (process + on-request
 * data widths) is the device's profile, and the ISDU on-request service framing layers on top;
 * lay those octets out per your device, then finalize / verify with this codec. The wire is a
 * UART at 4.8 / 38.4 / 230.4 kbit/s through an IO-Link transceiver (e.g. MAX14819 / L6360);
 * pure and host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IOLINK_H
#define PROTOCORE_IOLINK_H

#include "protocore_config.h"

#if PC_ENABLE_IOLINK

#include <stddef.h>
#include <stdint.h>

#define IOL_CHECKSUM_SEED 0x52u ///< checksum seed XORed with the first octet (spec A.1.6)

// M-sequence Control (MC) octet fields.
#define IOL_MC_READ 0x80u   ///< bit 7 set => read access
#define IOL_MC_WRITE 0x00u  ///< bit 7 clear => write access
#define IOL_CH_PROCESS 0u   ///< communication channel: Process Data
#define IOL_CH_PAGE 1u      ///< communication channel: Page (direct parameters)
#define IOL_CH_DIAGNOSIS 2u ///< communication channel: Diagnosis
#define IOL_CH_ISDU 3u      ///< communication channel: ISDU (on-request data)

// M-sequence types (CKT bits 7-6).
#define IOL_MSEQ_TYPE_0 0u
#define IOL_MSEQ_TYPE_1 1u
#define IOL_MSEQ_TYPE_2 2u

// Checksum / status (CKS) octet flags.
#define IOL_CKS_EVENT 0x80u      ///< bit 7: Device has an Event pending
#define IOL_CKS_PD_INVALID 0x40u ///< bit 6: Process Data invalid

#define IOL_CHECK_HIGH_MASK 0xC0u ///< the non-checksum (type / status) bits of a check octet
#define IOL_CHECK_SUM_MASK 0x3Fu  ///< the 6-bit checksum field of a check octet

// --- control-octet builders / decoders ---

/** @brief Build the M-sequence Control octet from access / channel / address (5-bit). */
uint8_t pc_iol_mc(bool read, uint8_t channel, uint8_t address);

/** @brief True if the MC octet requests a read. */
bool pc_iol_mc_is_read(uint8_t mc);

/** @brief Communication channel from an MC octet (IOL_CH_*). */
uint8_t pc_iol_mc_channel(uint8_t mc);

/** @brief Address (5-bit) from an MC octet. */
uint8_t pc_iol_mc_address(uint8_t mc);

/** @brief Build a CKT octet from an M-sequence type and a 6-bit checksum (use 0 before finalize). */
uint8_t pc_iol_ckt(uint8_t mseq_type, uint8_t checksum6);

/** @brief Build a CKS octet from the Event / PD-invalid flags and a 6-bit checksum. */
uint8_t pc_iol_cks(bool event, bool pd_invalid, uint8_t checksum6);

// --- checksum (spec A.1.6) ---

/**
 * @brief The compressed 6-bit SDCI checksum over @p msg (the check octet must already have its
 * low 6 bits set to 0). Seed 0x52, XOR every octet, then compress 8->6 per equation (A.1).
 */
uint8_t pc_iol_checksum6(const uint8_t *msg, size_t len);

/**
 * @brief Finalize a message in place: compute the checksum over @p msg (treating the check octet
 * at @p check_idx with checksum bits 0) and OR it into that octet, preserving its type / status
 * bits. Returns the finalized check octet.
 */
uint8_t pc_iol_finalize(uint8_t *msg, size_t len, size_t check_idx);

/**
 * @brief Verify a received message: recompute the checksum (masking off the check octet's
 * checksum bits) and compare it to the 6-bit checksum carried in the check octet at @p check_idx.
 */
bool pc_iol_verify(const uint8_t *msg, size_t len, size_t check_idx);

#endif // PC_ENABLE_IOLINK
#endif // PROTOCORE_IOLINK_H
