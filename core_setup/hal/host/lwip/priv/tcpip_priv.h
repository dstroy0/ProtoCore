// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The lwIP marshal into the stack's thread, standing in for silicon so the target path is the path
// a host test compiles and runs. There is no separate thread here, so the call runs inline - the
// same shape LWIP_TCPIP_CORE_LOCKING takes on the target, where the caller holds the core lock and
// the op runs on its own task.

#pragma once

#include "lwip/def.h"

struct tcpip_api_call_data
{
    int unused; ///< the real one carries a semaphore; nothing here waits
};

static inline err_t tcpip_api_call(err_t (*fn)(struct tcpip_api_call_data *), struct tcpip_api_call_data *call)
{
    return fn(call);
}
