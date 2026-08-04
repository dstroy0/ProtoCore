// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snp.h
 * @brief GE Fanuc SNP (Series Ninety Protocol) serial frame codec (PC_ENABLE_SNP).
 *
 * SNP is the GE Fanuc Series 90 (90-30 / 90-70) master-slave serial protocol over RS-485. A message is
 * a BCC-checked frame delimited by control characters:
 *
 *     [SOH-or-other-control][data...][checksum]
 *
 * SNP frames the payload with an ASCII/binary control byte, a length, the data, and an arithmetic-sum
 * BCC (the low byte of the sum of every framed byte). This builds/validates that frame so a device can
 * read/write registers on a Series 90 PLC; the RS-485 UART transport (and the SNP-X session setup) is
 * the remaining device step. Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_SNP_H
#define PROTOCORE_SNP_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SNP

/** @brief SNP control bytes (subset). */
// SNP control bytes: wire values compared/emitted, so integer constants in a namespacing struct.
#define SNP_ENQ 0x05 ///< enquiry / attach.
#define SNP_ACK 0x06 ///< acknowledge.
#define SNP_NAK 0x15 ///< negative acknowledge.
#define SNP_SOH 0x01 ///< start of header (a request/response frame).
#define SNP_EOT 0x04 ///< end of transmission.

/** @brief Arithmetic-sum BCC: the low 8 bits of the sum of @p len bytes. */
uint8_t pc_snp_bcc(const uint8_t *bytes, size_t len);

/**
 * @brief Build an SNP frame: [control][length][data...][BCC]. length is the data byte count.
 * @return the frame length (2 + data_len + 1), or 0 on overflow / bad args (data_len > 255).
 */
size_t pc_snp_build(uint8_t control, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap);

/** @brief A parsed SNP frame (data points into the input). */
typedef struct
{
    uint8_t control;
    const uint8_t *data;
    size_t data_len;
} SnpFrame;

/** @brief Validate the BCC and parse an SNP frame. @return true if the BCC matches and it is well-formed. */
proto_bool pc_snp_parse(const uint8_t *frame, size_t len, SnpFrame *out);

#endif // PC_ENABLE_SNP

PROTO_END_DECLS

#endif // PROTOCORE_SNP_H
