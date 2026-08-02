// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sep2.h
 * @brief IEEE 2030.5 (Smart Energy Profile 2.0) resource codec (PC_ENABLE_SEP2).
 *
 * IEEE 2030.5 (SEP 2.0) is the RESTful smart-grid protocol: a device is a set of HTTP resources encoded
 * as XML in the `urn:ieee:std:2030.5:ns` namespace. This builds the core resource documents into a
 * caller buffer, so the web server is a 2030.5 server / client over the existing HTTP stack:
 *
 *  - **DeviceCapability** - the root: hrefs to the function-set list resources the device offers.
 *  - **EndDevice** - a device registration (its sFDI/lFDI identity).
 *  - **DERControl** - a Distributed-Energy-Resource control event (interval + a target power factor /
 *    real-power setpoint), the payload that curtails or dispatches a DER.
 *
 * Pure text framing, zero heap, no stdlib, host-testable; values are XML-escaped where they can carry
 * text. The EXI binary encoding + the resource discovery walk layer on top.
 */

#ifndef PROTOCORE_SEP2_H
#define PROTOCORE_SEP2_H

#include "protocore_config.h"

#if PC_ENABLE_SEP2

/**
 * @brief Build a DeviceCapability document with hrefs to the EndDeviceList and the DER-program list.
 * @param poll_rate the recommended pollRate (seconds).
 * @return length written, or 0 on overflow.
 */
size_t pc_sep2_device_capability(uint32_t poll_rate, const char *edev_list_href, const char *derp_list_href, char *out,
                                 size_t cap);

/**
 * @brief Build an EndDevice document.
 * @param sfdi  the short-form device identifier (a 36-bit-ish decimal id).
 * @param lfdi  the long-form device identifier (hex string; borrowed).
 * @param href  the resource href (borrowed).
 * @return length written, or 0 on overflow.
 */
size_t pc_sep2_end_device(uint64_t sfdi, const char *lfdi, const char *href, char *out, size_t cap);

/**
 * @brief Build a DERControl event document.
 * @param mrid          the message RID (hex string; borrowed).
 * @param start         interval start (epoch seconds).
 * @param duration      interval duration (seconds).
 * @param opmod_target_w the real-power setpoint (opModFixedW), in watts (may be negative to absorb).
 * @return length written, or 0 on overflow.
 */
size_t pc_sep2_der_control(const char *mrid, uint32_t start, uint32_t duration, int32_t opmod_target_w, char *out,
                           size_t cap);

#endif // PC_ENABLE_SEP2
#endif // PROTOCORE_SEP2_H
