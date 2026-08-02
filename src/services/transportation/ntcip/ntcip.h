// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntcip.h
 * @brief NTCIP transportation-device object identifiers (PC_ENABLE_NTCIP).
 *
 * NTCIP (National Transportation Communications for ITS Protocol) rides SNMP: an NTCIP device is an SNMP
 * agent whose MIB carries the NTCIP object definitions. Since this library already ships an SNMP agent
 * (services/net/snmp), NTCIP is the object layer - the OID roots + the object arcs for the common device
 * classes:
 *
 *  - **NTCIP 1202** Actuated Signal Controllers: phase timing + live signal-group states.
 *  - **NTCIP 1203** Dynamic Message Signs: pushing / reading a message to a variable message board.
 *
 * All NTCIP objects live under `1.3.6.1.4.1.1206.4.2` (nema.tsxx). This provides those OID arc arrays and
 * a helper to build a full object OID (root + instance index), so an app registers them on the SNMP
 * agent with pc_snmp_agent_add_integer / _add_string / _add_dynamic. Pure OID data, zero heap, host-testable.
 */

#ifndef PROTOCORE_NTCIP_H
#define PROTOCORE_NTCIP_H

#include "protocore_config.h"

#if PC_ENABLE_NTCIP

/**
 * @brief NTCIP object roots under 1.3.6.1.4.1.1206.4.2 (private.enterprises.nema.transportation.devices).
 *
 * Each is the OID prefix of a scalar/column; the leaf/instance index is appended by
 * pc_ntcip_oid() (a scalar takes .0; a table column takes the row index).
 */
extern const uint32_t NTCIP_1202_MAX_PHASES[]; ///< maxPhases (1202): number of phases. len via _len.
extern const size_t NTCIP_1202_MAX_PHASES_LEN;
extern const uint32_t NTCIP_1202_PHASE_MIN_GREEN[]; ///< phaseMinimumGreen column (1202).
extern const size_t NTCIP_1202_PHASE_MIN_GREEN_LEN;
extern const uint32_t NTCIP_1202_PHASE_STATUS[]; ///< phaseStatusGroupGreens (1202): live green states.
extern const size_t NTCIP_1202_PHASE_STATUS_LEN;
extern const uint32_t NTCIP_1203_DMS_MAX_LENGTH[]; ///< dmsMaxMultiStringLength (1203).
extern const size_t NTCIP_1203_DMS_MAX_LENGTH_LEN;
extern const uint32_t NTCIP_1203_DMS_MESSAGE_MULTI[]; ///< dmsMessageMultiString column (1203): the MULTI text.
extern const size_t NTCIP_1203_DMS_MESSAGE_MULTI_LEN;

/**
 * @brief Build a full object OID by appending an instance index to a root.
 * @param root      the NTCIP object root arcs.
 * @param root_len  its length.
 * @param index     the instance/row index to append (use 0 for a scalar).
 * @param out       output arc buffer.
 * @param out_cap   its capacity (in arcs).
 * @return the total number of arcs written (root_len + 1), or 0 if it will not fit.
 */
size_t pc_ntcip_oid(const uint32_t *root, size_t root_len, uint32_t index, uint32_t *out, size_t out_cap);

#endif // PC_ENABLE_NTCIP
#endif // PROTOCORE_NTCIP_H
