// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iccp.h
 * @brief ICCP / TASE.2 (IEC 60870-6) inter-control-center telemetry codec (PC_ENABLE_ICCP).
 *
 * ICCP (TASE.2, IEC 60870-6) exchanges real-time power-grid telemetry between control centers. It is an
 * application profile *on top of MMS* (the shipped services/energy/mms): a TASE.2 "indication point" (a data
 * value with quality + timestamp) is transferred as an MMS Read on a named object. This codec builds the
 * TASE.2 data value - the standard `Data_Value` structure a Read carries:
 *
 *   Data_Value ::= SEQUENCE { value CHOICE {state/discrete/real/...}, flags DataFlags, timestamp }
 *
 * simplified here to the common **StateQ** (a discrete state 0..3 + a quality-flags byte) and **RealQ**
 * (an IEEE-754-ish scaled real + quality) indication points that most bilateral tables use. The result
 * is a BER blob the caller wraps in an MMS Read response (`pc_mms_read_response`). Pure, zero heap,
 * no stdlib, host-testable; the TASE.2 bilateral-table + MMS transport are the shipped services.
 */

#ifndef PROTOCORE_ICCP_H
#define PROTOCORE_ICCP_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_ICCP

/** @brief TASE.2 quality flags (DataFlags, the common bits). */
// TASE.2 quality/state wire values + the 2-bit quality mask, so integer constants in a struct.
#define ICCP_QUAL_VALID 0x00    ///< value is valid.
#define ICCP_QUAL_HELD 0x01     ///< value is held (frozen).
#define ICCP_QUAL_SUSPECT 0x02  ///< value is suspect.
#define ICCP_QUAL_NOTVALID 0x03 ///< value is not valid.
#define ICCP_QUAL_MASK 0x03
#define ICCP_STATE_BETWEEN 0x00 ///< StateQ: intermediate.
#define ICCP_STATE_OFF 0x01
#define ICCP_STATE_ON 0x02
#define ICCP_STATE_INVALID 0x03

/**
 * @brief Build a TASE.2 StateQ Data_Value: a discrete state + quality flags.
 * @param state  the 2-bit state (ICCP_STATE_*).
 * @param flags  the quality flags byte (ICCP_QUAL_*).
 * @param time   the 4-octet TimeStamp (seconds since 1970; big-endian), or null for no timestamp.
 * @return the BER length written, or 0 on overflow.
 *
 * Encodes `[A2 { 85 <stateAndQuality byte> [17 <4-octet time>] }]` (context-tagged StateQ structure).
 */
size_t pc_iccp_state_q(uint8_t state, uint8_t flags, const uint8_t time[4], uint8_t *out, size_t cap);

/**
 * @brief Build a TASE.2 RealQ Data_Value: a scaled real value (milli-units) + quality flags.
 * @param milli  the value in 1/1000 units (so 12.345 -> 12345), a signed 32-bit integer.
 * @param flags  the quality flags byte.
 * @param time   the 4-octet TimeStamp, or null.
 * @return the BER length written, or 0 on overflow.
 *
 * Encodes `[A3 { 02 <INTEGER milli> 85 <quality byte> [17 <time>] }]`.
 */
size_t pc_iccp_real_q(int32_t milli, uint8_t flags, const uint8_t time[4], uint8_t *out, size_t cap);

#endif // PC_ENABLE_ICCP

PROTO_END_DECLS

#endif // PROTOCORE_ICCP_H
