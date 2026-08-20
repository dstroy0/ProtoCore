// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_service.h
 * @brief Optional authenticated OTA firmware update (PROTOCORE_ENABLE_OTA).
 *
 * Registers a POST endpoint that streams a firmware image straight into the
 * platform update API via the parser's streaming-body hook
 * (http_parser_set_stream_hooks), so the image never has to fit in RAM. On a
 * successful flash the device responds and reboots into the new firmware.
 * Compiled to a no-op stub when PROTOCORE_ENABLE_OTA is 0 or the platform has no updater.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OTA_SERVICE_H
#define PROTOCORE_OTA_SERVICE_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Register an authenticated streaming OTA endpoint.
 *
 * Call after begin(). A `POST @p path` carrying a raw firmware image and valid
 * HTTP Basic credentials is streamed into `Update`; on success the device
 * replies `200` and reboots. Unauthorized or failed uploads get `401` / `400`.
 *
 * @param server  The running server (the route + stream hooks are installed on it).
 * @param path    URL to accept the upload on (e.g. "/update"). Persistent string.
 * @param user    Required HTTP Basic username.
 * @param pass    Required HTTP Basic password.
 *
 * @code
 * curl -u admin:s3cret --data-binary @firmware.bin http://<ip>/update
 * @endcode
 */
/** @brief What installing the upload route takes. */
typedef struct
{
    const char *path; ///< URL to accept the upload on (e.g. "/update"); a persistent string
    const char *user; ///< required HTTP Basic username
    const char *pass; ///< required HTTP Basic password
} OtaServiceArgs;

/**
 * @brief The firmware upload route.
 *
 * @var OtaServiceNs::args      what installing the route takes
 * @var OtaServiceNs::begin     install the route and the streaming-body hooks
 */
typedef struct
{
    OtaServiceArgs args;

    void (*const begin)(uint8_t *restrict work);
} OtaServiceNs;

/** @brief The one symbol this module exports. */
extern OtaServiceNs OtaService;

PROTOCORE_END_DECLS

#endif // PROTOCORE_OTA_SERVICE_H
