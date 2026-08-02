// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntcip.c
 * @brief NTCIP object identifiers (see ntcip.h).
 */

#include "services/transportation/ntcip/ntcip.h"

#if PC_ENABLE_NTCIP

// Everything hangs under 1.3.6.1.4.1.1206.4.2 = iso.org.dod.internet.private.enterprises.nema(1206)
// .transportation(4).devices(2). Under devices: .1 = protocols, .3 = global, and each device class has
// its own subtree - 1202 signals under .devices with the ASC objects, 1203 DMS likewise. The arc values
// below follow the published NTCIP object trees.

// --- NTCIP 1202 Actuated Signal Controller ---
// asc(1206.4.2.1) phase(1) ...
const uint32_t NTCIP_1202_MAX_PHASES[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2, 1, 1, 1};
const size_t NTCIP_1202_MAX_PHASES_LEN = sizeof(NTCIP_1202_MAX_PHASES) / sizeof(NTCIP_1202_MAX_PHASES[0]);

// phaseMinimumGreen: phaseTable(2) entry(1) col(4)
const uint32_t NTCIP_1202_PHASE_MIN_GREEN[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2, 1, 2, 1, 4};
const size_t NTCIP_1202_PHASE_MIN_GREEN_LEN =
    sizeof(NTCIP_1202_PHASE_MIN_GREEN) / sizeof(NTCIP_1202_PHASE_MIN_GREEN[0]);

// phaseStatusGroupGreens: phaseStatusGroup(5) table(2) entry(1) col(4)
const uint32_t NTCIP_1202_PHASE_STATUS[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2, 1, 5, 2, 1, 4};
const size_t NTCIP_1202_PHASE_STATUS_LEN = sizeof(NTCIP_1202_PHASE_STATUS) / sizeof(NTCIP_1202_PHASE_STATUS[0]);

// --- NTCIP 1203 Dynamic Message Sign ---
// dms(1206.4.2.3) dmsSignCfg(1) dmsMaxMultiStringLength(9)
const uint32_t NTCIP_1203_DMS_MAX_LENGTH[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2, 3, 1, 9};
const size_t NTCIP_1203_DMS_MAX_LENGTH_LEN = sizeof(NTCIP_1203_DMS_MAX_LENGTH) / sizeof(NTCIP_1203_DMS_MAX_LENGTH[0]);

// dmsMessageMultiString: dmsMessage(5) table(8) entry(1) col(3)
const uint32_t NTCIP_1203_DMS_MESSAGE_MULTI[] = {1, 3, 6, 1, 4, 1, 1206, 4, 2, 3, 5, 8, 1, 3};
const size_t NTCIP_1203_DMS_MESSAGE_MULTI_LEN =
    sizeof(NTCIP_1203_DMS_MESSAGE_MULTI) / sizeof(NTCIP_1203_DMS_MESSAGE_MULTI[0]);

size_t pc_ntcip_oid(const uint32_t *root, size_t root_len, uint32_t index, uint32_t *out, size_t out_cap)
{
    if (!root || !out || root_len == 0 || root_len + 1 > out_cap)
    {
        return 0;
    }
    for (size_t i = 0; i < root_len; i++)
    {
        out[i] = root[i];
    }
    out[root_len] = index;
    return root_len + 1;
}

#endif // PC_ENABLE_NTCIP
