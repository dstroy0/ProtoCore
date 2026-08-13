// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file phy.c
 * @brief The bound link driver, and nothing else.
 */

#include "idemIP/ethernet/phy.h"

// The one bound driver, owned by one instance (internal linkage). One named owner, unreachable
// from any other translation unit.
typedef struct
{
    const IdemIpPhyDriver *drv;
} IdemIpPhyCtx;
static IdemIpPhyCtx s_phy;

proto_bool idemip_phy_bind(const IdemIpPhyDriver *drv)
{
    // Every member is called without a null test on the hot path, so an incomplete driver is
    // refused here rather than faulting at the first frame.
    if (drv == NULL || drv->link == NULL || drv->send == NULL || drv->recv == NULL || drv->mac == NULL ||
        drv->mdio_read == NULL || drv->mdio_write == NULL)
    {
        return PROTO_FALSE;
    }
    s_phy.drv = drv;
    return PROTO_TRUE;
}

const IdemIpPhyDriver *idemip_phy_driver(void)
{
    return s_phy.drv;
}
