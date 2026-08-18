// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vendor_hal.h
 * @brief The accelerator HALs the selected arm answers with.
 *
 * Reached from config/platform/platform.h after the primitive types exist, because a HAL header is
 * declared in them. That is the only reason it is not in the arm's answers header: those resolve the
 * capability axis before any type does.
 *
 * A module calls protocore_sha_hw_block / protocore_aes_hw_block / protocore_rsa_modmul inside its
 * own hardware arm and includes nothing to get them, so nothing under src/ names a HAL path. Each
 * include is gated on its own capability, which the arm's answers header stated.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_VENDOR_HAL_H
#define PROTOCORE_VENDOR_HAL_H

#if PROTOCORE_VENDOR_ESP
#include "test/core_setup/hal/esp/esp_aes_hal.h"    // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_HAS_HW_AES
#include "test/core_setup/hal/esp/esp_crypto_hal.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_RSA_MODMUL_HW
#include "test/core_setup/hal/esp/esp_sha_hal.h"    // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_HAS_HW_SHA
#elif PROTOCORE_HOST
#include "test/core_setup/hal/host/host_aes_hal.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_HAS_HW_AES
#include "test/core_setup/hal/host/host_crypto_hal.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_RSA_MODMUL_HW
#include "test/core_setup/hal/host/host_hw_reg.h"     // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - the modelled register bus
#include "test/core_setup/hal/host/host_sha_hal.h" // PROTOCORE_ALLOW_LATE_INCLUDE: ordered - gated on PROTOCORE_HAS_HW_SHA
#endif

#endif // PROTOCORE_VENDOR_HAL_H
